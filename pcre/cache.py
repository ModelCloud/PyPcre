# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Pattern caching helpers for the high level PCRE wrapper."""

from __future__ import annotations

from collections import OrderedDict
from threading import RLock
from typing import Any, Callable, Tuple, TypeVar

from . import cpcre2 as _pcre2


T = TypeVar("T")

_DEFAULT_CACHE_LIMIT = 128
_CACHE_LIMIT: int | None = _DEFAULT_CACHE_LIMIT
_PATTERN_CACHE: OrderedDict[Tuple[Any, int, bool], T] = OrderedDict()
_PATTERN_CACHE_LOCK = RLock()


def cached_compile(
    pattern: Any,
    flags: int,
    wrapper: Callable[["_pcre2.Pattern"], T],
    *,
    jit: bool,
) -> T:
    """Compile *pattern* with *flags*, caching wrapper results when hashable."""

    cache_limit = _CACHE_LIMIT
    if cache_limit == 0:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    try:
        key = (pattern, flags, bool(jit))
        hash(key)
    except TypeError:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    with _PATTERN_CACHE_LOCK:
        cached = _PATTERN_CACHE.get(key)
        if cached is not None:
            _PATTERN_CACHE.move_to_end(key)
            return cached

    compiled = wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    with _PATTERN_CACHE_LOCK:
        if _CACHE_LIMIT == 0:
            return compiled
        existing = _PATTERN_CACHE.get(key)
        if existing is not None:
            _PATTERN_CACHE.move_to_end(key)
            return existing
        _PATTERN_CACHE[key] = compiled
        if (_CACHE_LIMIT is not None) and len(_PATTERN_CACHE) > _CACHE_LIMIT:
            _PATTERN_CACHE.popitem(last=False)
        return compiled


def clear_cache() -> None:
    """Clear cached compiled patterns plus backend match-data and JIT stacks."""

    with _PATTERN_CACHE_LOCK:
        _PATTERN_CACHE.clear()

    _pcre2.clear_match_data_cache()
    _pcre2.clear_jit_stack_cache()


def set_cache_limit(limit: int | None) -> None:
    """Adjust the maximum number of cached patterns.

    Passing ``None`` removes the limit. ``0`` disables caching entirely.
    """

    global _CACHE_LIMIT

    if limit is None:
        new_limit: int | None = None
    else:
        try:
            new_limit = int(limit)
        except TypeError as exc:  # pragma: no cover - defensive
            raise TypeError("cache limit must be an int or None") from exc
        if new_limit < 0:
            raise ValueError("cache limit must be >= 0 or None")

    with _PATTERN_CACHE_LOCK:
        _CACHE_LIMIT = new_limit
        if new_limit == 0:
            _PATTERN_CACHE.clear()
        elif new_limit is not None:
            while len(_PATTERN_CACHE) > new_limit:
                _PATTERN_CACHE.popitem(last=False)


def get_cache_limit() -> int | None:
    """Return the current cache limit (``None`` means unlimited)."""

    return _CACHE_LIMIT
