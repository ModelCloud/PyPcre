# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Helpers for managing Python-only flag extensions."""

from __future__ import annotations

from typing import Dict

from . import cpcre2 as _pcre2


def _collect_native_flag_values() -> list[int]:
    values: list[int] = []
    for name in dir(_pcre2):
        if not name.startswith("PCRE2_"):
            continue
        value = getattr(_pcre2, name)
        if isinstance(value, int):
            values.append(value)
    return values


def _next_power_of_two(value: int) -> int:
    if value <= 0:
        return 1
    return 1 << (value.bit_length())


_NATIVE_FLAG_VALUES = _collect_native_flag_values()
_EXTRA_BASE = _next_power_of_two(max(_NATIVE_FLAG_VALUES, default=0))

NO_UTF: int = _EXTRA_BASE
NO_UCP: int = _EXTRA_BASE << 1
JIT: int = _EXTRA_BASE << 2
NO_JIT: int = _EXTRA_BASE << 3
THREADS: int = _EXTRA_BASE << 4
NO_THREADS: int = _EXTRA_BASE << 5

PY_ONLY_FLAG_MEMBERS: Dict[str, int] = {
    "NO_UTF": NO_UTF,
    "NO_UCP": NO_UCP,
    "JIT": JIT,
    "NO_JIT": NO_JIT,
    "THREADS": THREADS,
    "NO_THREADS": NO_THREADS,
}

PY_ONLY_FLAG_MASK: int = NO_UTF | NO_UCP | JIT | NO_JIT | THREADS | NO_THREADS


def strip_py_only_flags(flags: int) -> int:
    """Remove Python-only option bits that the C engine does not understand."""

    return flags & ~PY_ONLY_FLAG_MASK
