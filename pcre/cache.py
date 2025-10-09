# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Pattern caching helpers for the high level PCRE wrapper."""

from __future__ import annotations

from collections import OrderedDict
from threading import local
from typing import Any, Callable, Tuple, TypeVar, cast

import pcre_ext_c as _pcre2


T = TypeVar("T")

_DEFAULT_CACHE_LIMIT = 16


class _CacheState(local):
    """Thread-local cache state holding the cache store and limit."""

    def __init__(self) -> None:
        self.cache_limit: int | None = _DEFAULT_CACHE_LIMIT
        self.pattern_cache: OrderedDict[Tuple[Any, int, bool], Any] = OrderedDict()


_THREAD_LOCAL = _CacheState()


def cached_compile(
    pattern: Any,
    flags: int,
    wrapper: Callable[["_pcre2.Pattern"], T],
    *,
    jit: bool,
) -> T:
    """Compile *pattern* with *flags*, caching wrapper results when hashable."""

    cache_limit = _THREAD_LOCAL.cache_limit
    if cache_limit == 0:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    try:
        key = (pattern, flags, bool(jit))
        hash(key)
    except TypeError:
        return wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    cache = _THREAD_LOCAL.pattern_cache
    cached = cache.get(key)
    if cached is not None:
        cache.move_to_end(key)
        return cast(T, cached)

    compiled = wrapper(_pcre2.compile(pattern, flags=flags, jit=jit))

    cache_limit = _THREAD_LOCAL.cache_limit
    if cache_limit == 0:
        return compiled

    cache = _THREAD_LOCAL.pattern_cache
    existing = cache.get(key)
    if existing is not None:
        cache.move_to_end(key)
        return cast(T, existing)

    cache[key] = compiled
    if (cache_limit is not None) and len(cache) > cache_limit:
        cache.popitem(last=False)
    return compiled


def clear_cache() -> None:
    """Clear the cached compiled patterns for the current thread."""

    _THREAD_LOCAL.pattern_cache.clear()


def set_cache_limit(limit: int | None) -> None:
    """Adjust the maximum number of cached patterns.

    Passing ``None`` removes the limit. ``0`` disables caching entirely.
    """

    if limit is None:
        new_limit: int | None = None
    else:
        try:
            new_limit = int(limit)
        except TypeError as exc:  # pragma: no cover - defensive
            raise TypeError("cache limit must be an int or None") from exc
        if new_limit < 0:
            raise ValueError("cache limit must be >= 0 or None")

    _THREAD_LOCAL.cache_limit = new_limit

    cache = _THREAD_LOCAL.pattern_cache
    if new_limit == 0:
        cache.clear()
    elif new_limit is not None:
        while len(cache) > new_limit:
            cache.popitem(last=False)


def get_cache_limit() -> int | None:
    """Return the current cache limit (``None`` means unlimited)."""

    return _THREAD_LOCAL.cache_limit
