# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import pcre
import pytest


_CANONICAL_FLAG_NAMES = sorted(
    name for name, flag in pcre.Flag.__members__.items() if flag.name == name
)

_ALIAS_FLAG_NAMES = sorted(
    name for name, flag in pcre.Flag.__members__.items() if flag.name != name
)


@pytest.mark.parametrize("flag_name", _CANONICAL_FLAG_NAMES)
def test_compile_accepts_each_canonical_flag(flag_name: str) -> None:
    flag = pcre.Flag[flag_name]
    try:
        compiled = pcre.compile(b"a", flags=flag)
    except pcre.PcreError as exc:
        if getattr(exc, "macro", None) == "PCRE2_ERROR_BAD_OPTIONS":
            pytest.skip(f"flag {flag_name} unsupported by current PCRE build: {exc}")
        raise

    assert isinstance(compiled, pcre.Pattern)


@pytest.mark.parametrize("alias_name", _ALIAS_FLAG_NAMES)
def test_alias_flags_round_trip(alias_name: str) -> None:
    alias_flag = pcre.Flag[alias_name]
    canonical_flag = pcre.Flag[alias_flag.name]
    assert alias_flag is canonical_flag

    compiled = pcre.compile(b"a")
    try:
        compiled.search(b"a", options=alias_flag)
    except pcre.PcreError as exc:
        if getattr(exc, "macro", None) in {"PCRE2_ERROR_BADOPTION", "PCRE2_ERROR_BAD_OPTIONS"}:
            pytest.skip(f"match option {alias_name} unsupported by current PCRE build: {exc}")
        raise

