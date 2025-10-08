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

_MAX_PATTERN_CACHE = 2048
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
        existing = _PATTERN_CACHE.get(key)
        if existing is not None:
            _PATTERN_CACHE.move_to_end(key)
            return existing
        _PATTERN_CACHE[key] = compiled
        if len(_PATTERN_CACHE) > _MAX_PATTERN_CACHE:
            _PATTERN_CACHE.popitem(last=False)
        return compiled


def clear_cache() -> None:
    """Clear all cached compiled pattern wrappers."""

    with _PATTERN_CACHE_LOCK:
        _PATTERN_CACHE.clear()
