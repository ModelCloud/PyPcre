# SPDX-FileCopyrightText: 2026 ModelCloud.ai
# SPDX-FileCopyrightText: 2026 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Capture-count limits and named-reference bounds in Match.expand (#130)."""

from __future__ import annotations

import pytest

import pcre
from pcre import Flag


def test_65535_capture_groups_are_rejected():
    # PCRE2 accepts 65535 groups (LINK_SIZE 3/4 builds), but a match data
    # block can only report groups 0..65534. With the default LINK_SIZE 2 the
    # pattern is already too large to compile; either way it must not compile.
    pattern = "x|" + "()" * 65534 + "(?P<n>y)"
    with pytest.raises(pcre.PcreError):
        pcre.compile(pattern, Flag.NO_JIT)


@pytest.mark.parametrize("flags", [0, Flag.NO_JIT], ids=["jit", "no_jit"])
def test_unmatched_named_group_expands_empty(flags):
    pattern = pcre.compile("x|" + "()" * 200 + "(?P<n>y)", flags)
    match = pattern.search("x")
    assert match is not None
    assert pattern.groupindex["n"] == 201
    assert len(match.groups()) == 201
    assert match["n"] is None
    assert match.expand(r"[\g<n>][\g<n>]") == "[][]"


def test_duplicate_names_prefer_participating_group():
    pattern = pcre.compile(r"(?J)(?:(?P<n>a)|(?P<n>b))")
    assert pattern.search("b").expand(r"<\g<n>>") == "<b>"
    assert pattern.search("a").expand(r"<\g<n>>\g<n>") == "<a>a"
