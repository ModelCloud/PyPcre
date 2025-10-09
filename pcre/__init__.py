# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""High level Python bindings for PCRE2.

This package exposes a Pythonic API on top of the low-level C extension found in
``pcre.cpcre2``. The wrapper keeps friction low compared to :mod:`re` while
surfacing PCRE2-specific flags and behaviours.
"""

from __future__ import annotations

import re as _std_re
from enum import IntEnum, IntFlag
from typing import Any

from . import cpcre2
from .cache import get_cache_limit, set_cache_limit
from .flags import PY_ONLY_FLAG_MEMBERS
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
    module_fullmatch,
    parallel_map,
    search,
    split,
    sub,
    subn,
)

from .threads import configure_thread_pool, shutdown_thread_pool
from .threads import configure_threads


__version__ = getattr(cpcre2, "__version__", "0.0")

_cpu_ascii_vector_mode = getattr(cpcre2, "_cpu_ascii_vector_mode", None)

_FLAG_MEMBERS: dict[str, int] = {}
_ERROR_CODE_MEMBERS: dict[str, int] = {}

for _name in dir(cpcre2):
    if not _name.startswith("PCRE2_"):
        continue
    _value = getattr(cpcre2, _name)
    if not isinstance(_value, int):
        continue

    if _name == "PCRE2_CODE_UNIT_WIDTH":
        continue

    if _name.startswith("PCRE2_ERROR_"):
        _ERROR_CODE_MEMBERS[_name.removeprefix("PCRE2_ERROR_")] = _value
        continue

    _FLAG_MEMBERS[_name.removeprefix("PCRE2_")] = _value

_FLAG_MEMBERS.update(PY_ONLY_FLAG_MEMBERS)

if _FLAG_MEMBERS:
    Flag = IntFlag("Flag", _FLAG_MEMBERS)
    Flag.__doc__ = "Pythonic IntFlag aliases for PCRE2 option constants."
else:  # pragma: no cover - defensive fallback that should never trigger
    class Flag(IntFlag):
        """Empty IntFlag placeholder when no PCRE2 constants are available."""


if _ERROR_CODE_MEMBERS:
    PcreErrorCode = IntEnum("PcreErrorCode", _ERROR_CODE_MEMBERS)
    PcreErrorCode.__doc__ = "IntEnum exposing PCRE2 error identifiers."
else:  # pragma: no cover - defensive fallback that should never trigger
    class PcreErrorCode(IntEnum):
        """Empty IntEnum placeholder when PCRE2 error constants are unavailable."""


def _error_code_property(self) -> PcreErrorCode | None:
    try:
        return PcreErrorCode(self.code)
    except (ValueError, TypeError):
        return None


PcreError.error_code = property(_error_code_property)


_EXPORTED_ERROR_CLASSES: list[str] = []
for _name in dir(cpcre2):
    if _name.startswith("PcreError") and _name != "PcreError":
        globals()[_name] = getattr(cpcre2, _name)
        _EXPORTED_ERROR_CLASSES.append(_name)


purge = clear_cache
error = PcreError
PatternError = PcreError


def escape(pattern: Any) -> Any:
    """Escape special characters in *pattern* using :mod:`re` semantics."""

    return _std_re.escape(pattern)


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
    "set_cache_limit",
    "get_cache_limit",
    "compile",
    "match",
    "search",
    "fullmatch",
    "module_fullmatch",
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
]

__all__ += _EXPORTED_ERROR_CLASSES

if _cpu_ascii_vector_mode is not None:
    globals()["_cpu_ascii_vector_mode"] = _cpu_ascii_vector_mode
    __all__.append("_cpu_ascii_vector_mode")
