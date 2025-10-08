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
from enum import IntFlag
from typing import Any

from . import cpcre2
from .flags import PY_ONLY_FLAG_MEMBERS
from .pcre import (
    Match,
    Pattern,
    PcreError,
    clear_cache,
    compile,
    findall,
    finditer,
    fullmatch,
    match,
    search,
    split,
    sub,
    subn,
)


__version__ = getattr(cpcre2, "__version__", "0.0")

_FLAG_MEMBERS: dict[str, int] = {}

for _name in dir(cpcre2):
    if not _name.startswith("PCRE2_"):
        continue
    _value = getattr(cpcre2, _name)

    if isinstance(_value, int) and _name != "PCRE2_CODE_UNIT_WIDTH":
        _FLAG_MEMBERS[_name.removeprefix("PCRE2_")] = _value

_FLAG_MEMBERS.update(PY_ONLY_FLAG_MEMBERS)

if _FLAG_MEMBERS:
    Flag = IntFlag("Flag", _FLAG_MEMBERS)
    Flag.__doc__ = "Pythonic IntFlag aliases for PCRE2 option constants."
else:  # pragma: no cover - defensive fallback that should never trigger
    class Flag(IntFlag):
        """Empty IntFlag placeholder when no PCRE2 constants are available."""


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
    "clear_cache",
    "purge",
    "compile",
    "match",
    "search",
    "fullmatch",
    "finditer",
    "findall",
    "split",
    "sub",
    "subn",
    "error",
    "PatternError",
    "Flag",
    "escape",
]
