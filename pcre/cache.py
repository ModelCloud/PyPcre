# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Pattern caching helpers for the high level PCRE wrapper."""

from __future__ import annotations

import os
from enum import Enum
from threading import RLock, local
from typing import Any, Callable, TypeVar, cast
from weakref import WeakSet

import pcre_ext_c as _pcre2

T = TypeVar("T")

_DEFAULT_THREAD_CACHE_LIMIT = 32
_DEFAULT_GLOBAL_CACHE_LIMIT = 128
_HARD_CACHE_ENTRY_LIMIT = 256
_MAX_CACHE_INPUT_UNITS = 64 * 1024

_CACHE_EPOCH = globals().get("_CACHE_EPOCH", 0)
_CACHE_EPOCH_LOCK = globals().get("_CACHE_EPOCH_LOCK", RLock())
_CACHE_CONTROL_LOCK = globals().get("_CACHE_CONTROL_LOCK", RLock())
_CACHE_CONTROL_CALLBACKS: WeakSet[Callable[[], None]] = globals().get(
    "_CACHE_CONTROL_CALLBACKS", WeakSet()
)


def get_cache_epoch() -> int:
    """Return the process-wide invalidation generation for auxiliary caches."""

    return _CACHE_EPOCH


def _bump_cache_epoch() -> int:
    global _CACHE_EPOCH

    with _CACHE_EPOCH_LOCK:
        _CACHE_EPOCH += 1
        return _CACHE_EPOCH


def register_cache_control(callback: Callable[[], None]) -> None:
    """Register a current-context cache clearer/trim callback."""

    with _CACHE_CONTROL_LOCK:
        _CACHE_CONTROL_CALLBACKS.add(callback)


def _notify_cache_control() -> None:
    with _CACHE_CONTROL_LOCK:
        callbacks = tuple(_CACHE_CONTROL_CALLBACKS)
    for callback in callbacks:
        callback()


def cache_input_allowed(value: Any) -> bool:
    """Reject oversized immutable inputs from persistent caches."""

    return not isinstance(value, (str, bytes)) or len(value) <= _MAX_CACHE_INPUT_UNITS


def _bounded_cache_limit(limit: int | None) -> int:
    if limit is None:
        return _HARD_CACHE_ENTRY_LIMIT
    return min(limit, _HARD_CACHE_ENTRY_LIMIT)


class _CacheStrategy(str, Enum):
    THREAD_LOCAL = "thread-local"
    GLOBAL = "global"


class _ThreadCacheState(local):
    """Thread-local cache state holding the cache store and limit."""

    def __init__(self) -> None:
        self.cache_limit: int | None = _DEFAULT_THREAD_CACHE_LIMIT
        self.pattern_cache: dict[tuple[Any, int, bool], Any] = {}
        self.epoch = get_cache_epoch()


class _GlobalCacheState:
    """Process-wide cache state mirroring the historic global cache."""

    __slots__ = ("cache_limit", "lock", "pattern_cache")

    def __init__(self) -> None:
        self.cache_limit: int | None = _DEFAULT_GLOBAL_CACHE_LIMIT
        self.pattern_cache: dict[tuple[Any, int, bool], Any] = {}
        self.lock = RLock()


_THREAD_LOCAL = _ThreadCacheState()
_GLOBAL_STATE = _GlobalCacheState()
_CACHE_STRATEGY = _CacheStrategy.THREAD_LOCAL


def _normalize_strategy(value: str) -> _CacheStrategy:
    try:
        return _CacheStrategy(value)
    except ValueError as exc:  # pragma: no cover - defensive
        raise ValueError("cache strategy must be 'thread-local' or 'global'") from exc


def _env_flag_is_true(value: str | None) -> bool:
    if value is None or value == "":
        return False
    return value[0] not in {"0", "f", "F", "n", "N"}


if _env_flag_is_true(os.getenv("PYPCRE_CACHE_PATTERN_GLOBAL")):
    _CACHE_STRATEGY = _CacheStrategy.GLOBAL
else:
    _CACHE_STRATEGY = _CacheStrategy.THREAD_LOCAL


def cache_strategy(strategy: str | None = None) -> str:
    """Select or query the caching strategy.

    Passing ``None`` returns the active strategy name. Supported strategies are
    ``"thread-local"`` (default) and ``"global"``.

    Switching strategies after cache usage is not supported and will raise a
    :class:`RuntimeError`.
    """

    if strategy is None:
        return _CACHE_STRATEGY.value

    desired = _normalize_strategy(strategy)
    if desired is _CACHE_STRATEGY:
        return _CACHE_STRATEGY.value

    raise RuntimeError(
        "cache strategy is fixed at import time; set PYPCRE_CACHE_PATTERN_GLOBAL=1 "
        "before importing pcre to enable the global cache"
    )


def _cached_compile_thread_local(
    pattern: Any,
    flags: int,
    wrapper: Callable[[_pcre2.Pattern], T],
    *,
    jit: bool,
) -> T:
    current_epoch = get_cache_epoch()
    if _THREAD_LOCAL.epoch != current_epoch:
        _THREAD_LOCAL.pattern_cache.clear()
        _THREAD_LOCAL.epoch = current_epoch
        _pcre2.clear_pattern_cache()
        _pcre2.clear_match_data_cache()
        _pcre2.clear_jit_stack_cache()

    cache_limit = _bounded_cache_limit(_THREAD_LOCAL.cache_limit)
    if not cache_input_allowed(pattern):
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))
    if cache_limit == 0:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    key = (pattern, flags, bool(jit))
    cache = _THREAD_LOCAL.pattern_cache
    try:
        cached = cache[key]
    except KeyError:
        compiled = wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))
        active_limit = _bounded_cache_limit(_THREAD_LOCAL.cache_limit)
        if _THREAD_LOCAL.epoch != current_epoch or active_limit == 0:
            return compiled
        if len(cache) >= active_limit:
            cache.pop(next(iter(cache)))
        cache[key] = compiled
        return compiled
    except TypeError:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))
    else:
        return cast(T, cached)


def _cached_compile_global(
    pattern: Any,
    flags: int,
    wrapper: Callable[[_pcre2.Pattern], T],
    *,
    jit: bool,
) -> T:
    cache_limit = _bounded_cache_limit(_GLOBAL_STATE.cache_limit)
    if not cache_input_allowed(pattern):
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))
    if cache_limit == 0:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    key = (pattern, flags, bool(jit))
    lock = _GLOBAL_STATE.lock
    with lock:
        try:
            cached = _GLOBAL_STATE.pattern_cache[key]
        except KeyError:
            pass
        except TypeError:
            return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))
        else:
            return cast(T, cached)

    compile_epoch = get_cache_epoch()
    compiled = wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    with lock:
        if _GLOBAL_STATE.cache_limit == 0 or compile_epoch != get_cache_epoch():
            return compiled
        try:
            existing = _GLOBAL_STATE.pattern_cache[key]
        except KeyError:
            active_limit = _bounded_cache_limit(_GLOBAL_STATE.cache_limit)
            if len(_GLOBAL_STATE.pattern_cache) >= active_limit:
                _GLOBAL_STATE.pattern_cache.pop(next(iter(_GLOBAL_STATE.pattern_cache)))
            _GLOBAL_STATE.pattern_cache[key] = compiled
        except TypeError:
            return compiled
        else:
            return cast(T, existing)
        return compiled


def cached_compile(
    pattern: Any,
    flags: int,
    wrapper: Callable[[_pcre2.Pattern], T],
    *,
    jit: bool,
) -> T:
    """Compile *pattern* with *flags*, caching wrapper results when hashable."""

    if _CACHE_STRATEGY is _CacheStrategy.THREAD_LOCAL:
        return _cached_compile_thread_local(pattern, flags, wrapper, jit=jit)
    return _cached_compile_global(pattern, flags, wrapper, jit=jit)


def clear_cache() -> None:
    """Clear the cached compiled patterns and backend caches for the active strategy."""

    current_epoch = _bump_cache_epoch()

    if _CACHE_STRATEGY is _CacheStrategy.THREAD_LOCAL:
        _THREAD_LOCAL.pattern_cache.clear()
        _THREAD_LOCAL.epoch = current_epoch
    else:
        with _GLOBAL_STATE.lock:
            _GLOBAL_STATE.pattern_cache.clear()

    _pcre2.clear_pattern_cache()
    _pcre2.clear_match_data_cache()
    _pcre2.clear_jit_stack_cache()
    _notify_cache_control()


def set_cache_limit(limit: int | None) -> None:
    """Adjust the maximum number of cached patterns for the active strategy."""

    if limit is None:
        new_limit: int | None = None
    else:
        try:
            new_limit = int(limit)
        except TypeError as exc:  # pragma: no cover - defensive
            raise TypeError("cache limit must be an int or None") from exc
        if new_limit < 0:
            raise ValueError("cache limit must be >= 0 or None")

    if _CACHE_STRATEGY is _CacheStrategy.THREAD_LOCAL:
        _THREAD_LOCAL.cache_limit = new_limit
        cache = _THREAD_LOCAL.pattern_cache
        effective_limit = _bounded_cache_limit(new_limit)
        if effective_limit == 0:
            cache.clear()
            _pcre2.clear_pattern_cache()
        else:
            while len(cache) > effective_limit:
                cache.pop(next(iter(cache)))
    else:
        _bump_cache_epoch()
        with _GLOBAL_STATE.lock:
            _GLOBAL_STATE.cache_limit = new_limit
            cache = _GLOBAL_STATE.pattern_cache
            effective_limit = _bounded_cache_limit(new_limit)
            if effective_limit == 0:
                cache.clear()
            else:
                while len(cache) > effective_limit:
                    cache.pop(next(iter(cache)))

    _notify_cache_control()


def get_cache_limit() -> int | None:
    """Return the requested limit (``None`` uses the hard safety ceiling)."""

    if _CACHE_STRATEGY is _CacheStrategy.THREAD_LOCAL:
        return _THREAD_LOCAL.cache_limit
    return _GLOBAL_STATE.cache_limit


def get_effective_cache_limit() -> int:
    """Return the bounded entry limit applied to every high-level cache."""

    return _bounded_cache_limit(get_cache_limit())


# The backend has already been configured during module import; rely on its
# reported strategy to keep this helper in sync.
