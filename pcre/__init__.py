# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""High level Python bindings for PCRE2.

This package exposes a Pythonic API on top of the low-level C extension found in
``pcre_ext_c``. The wrapper keeps friction low compared to :mod:`re` while
surfacing PCRE2-specific flags and behaviours.
"""

from __future__ import annotations

import re as _std_re
from typing import Any

import pcre_ext_c as _backend

from . import error as _error_module
from .cache import cache_strategy, get_cache_limit, set_cache_limit
from .error import ERRORS_BY_CODE, ERRORS_BY_MACRO, PcreErrorCode
from .flags import Flag
from .pcre import (
    Match,
    Pattern,
    PcreError,
    clear_cache,
    compile,
    configure,
    findall,
    finditer,
    fullmatch,
    match,
    parallel_map,
    search,
    split,
    sub,
    subn,
)
from .threads import configure_thread_pool, configure_threads, shutdown_thread_pool


pcre_ext_c = _backend

__version__ = getattr(_backend, "__version__", "0.0")

_cpu_ascii_vector_mode = getattr(_backend, "_cpu_ascii_vector_mode", None)


def _error_code_property(self) -> PcreErrorCode | None:
    try:
        return PcreErrorCode(self.code)
    except (ValueError, TypeError):
        return None


PcreError.error_code = property(_error_code_property)
del _error_code_property

# Re-export the statically declared PyError* aliases from the error module.
for _name in _error_module.__all__:
    globals()[_name] = getattr(_error_module, _name)

_EXPORTED_ERROR_CLASSES: list[str] = []
for _name in dir(_backend):
    if _name.startswith("PcreError") and _name != "PcreError":
        globals()[_name] = getattr(_backend, _name)
        _EXPORTED_ERROR_CLASSES.append(_name)

purge = clear_cache
error = PcreError
PatternError = PcreError


def escape(pattern: Any) -> Any:
    """Escape special characters in *pattern* using :mod:`re` semantics."""

    return _std_re.escape(pattern)


# Compat: expose stdlib-style flag constants so migrating `re` users can
# continue referencing familiar names. Prefer `pcre.Flag` for new code.
_FLAG_ZERO = Flag(0)
_FLAG_COMPAT_ALIASES = {
    "IGNORECASE": Flag.CASELESS,
    "I": Flag.CASELESS,
    "MULTILINE": Flag.MULTILINE,
    "M": Flag.MULTILINE,
    "DOTALL": Flag.DOTALL,
    "S": Flag.DOTALL,
    "VERBOSE": Flag.EXTENDED,
    "X": Flag.EXTENDED,
    "ASCII": Flag.NO_UTF | Flag.NO_UCP,
    "A": Flag.NO_UTF | Flag.NO_UCP,
    "UNICODE": _FLAG_ZERO,
    "U": _FLAG_ZERO,
}

for _alias, _flag in _FLAG_COMPAT_ALIASES.items():
    globals()[_alias] = _flag


__all__ = [
    "Pattern",
    "Match",
    "PcreError",
    "PcreErrorCode",
    "clear_cache",
    "purge",
    "configure",
    "configure_threads",
    "configure_thread_pool",
    "cache_strategy",
    "set_cache_limit",
    "get_cache_limit",
    "compile",
    "match",
    "search",
    "fullmatch",
    "finditer",
    "findall",
    "parallel_map",
    "split",
    "sub",
    "subn",
    "shutdown_thread_pool",
    "error",
    "PatternError",
    "Flag",
    "escape",
    "ERRORS_BY_CODE",
    "ERRORS_BY_MACRO",
]

__all__ += list(_FLAG_COMPAT_ALIASES.keys())

__all__ += list(_error_module.__all__)
__all__ += _EXPORTED_ERROR_CLASSES

if _cpu_ascii_vector_mode is not None:
    globals()["_cpu_ascii_vector_mode"] = _cpu_ascii_vector_mode
    __all__.append("_cpu_ascii_vector_mode")
