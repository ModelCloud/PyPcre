# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
from concurrent.futures import ThreadPoolExecutor

import pcre_ext_c as raw
import pytest

import pcre


@pytest.mark.parametrize(
    "pattern,subject,method",
    [
        (r"(a(b))", "ab", "search"),
        (r"((a)|(b))", "a", "search"),
        (r"((a)|(b))", "b", "fullmatch"),
        (r"(a)(b)", "ab", "match"),
        (r"(?=(a))(a)", "a", "search"),
        (r"((?=(a))a)", "a", "fullmatch"),
        (r"(?P<outer>a(?P<inner>b))", "ab", "search"),
        (r"(?P<first>a)(?P<second>b)", "ab", "fullmatch"),
        (r"(a(?:b(c))?)", "abc", "search"),
        (r"(a)?b", "b", "search"),
    ],
)
def test_lastindex_and_lastgroup_match_re(
    pattern: str,
    subject: str,
    method: str,
) -> None:
    expected = getattr(re.compile(pattern), method)(subject)
    actual = getattr(pcre.compile(pattern, pcre.Flag.NO_JIT), method)(subject)
    assert expected is not None
    assert actual is not None
    assert actual.lastindex == expected.lastindex
    assert actual.lastgroup == expected.lastgroup
    # The lazy replay result is cached and remains stable on repeated access.
    assert actual.lastindex == expected.lastindex
    assert actual.lastgroup == expected.lastgroup


def test_finditer_lastindex_preserves_empty_match_retry_options() -> None:
    pattern_text = r"(?:(?P<empty>)|(?P<letter>a))"
    expected = list(re.compile(pattern_text).finditer("a"))
    actual = list(pcre.compile(pattern_text, pcre.Flag.NO_JIT).finditer("a"))
    assert [(match.span(), match.lastindex, match.lastgroup) for match in actual] == [
        (match.span(), match.lastindex, match.lastgroup) for match in expected
    ]


def test_lastindex_replay_preserves_explicit_match_options() -> None:
    pattern = raw.compile(
        b"(?P<at_end>a$)|(?P<fallback>a)",
        jit=False,
    )
    match = pattern.search(b"a", options=raw.PCRE2_NOTEOL)
    assert match is not None
    assert match.groups() == (None, b"a")
    assert match.lastindex == 2
    assert match.lastgroup == "fallback"


@pytest.mark.parametrize("subject,expected_index", [("a", 1), ("b", 2)])
def test_lastgroup_supports_duplicate_pcre_names(
    subject: str,
    expected_index: int,
) -> None:
    pattern = pcre.compile(r"(?P<x>a)|(?P<x>b)", pcre.Flag.DUPNAMES | pcre.Flag.NO_JIT)
    match = pattern.fullmatch(subject)
    assert match is not None
    assert match.lastindex == expected_index
    assert match.lastgroup == "x"


def test_lastindex_from_jit_match_uses_exact_interpreter_replay() -> None:
    pattern_text = r"(?P<outer>a(?P<inner>b))"
    expected = re.fullmatch(pattern_text, "ab")
    actual = pcre.compile(pattern_text, pcre.Flag.JIT).fullmatch("ab")
    assert expected is not None
    assert actual is not None
    assert actual.lastindex == expected.lastindex
    assert actual.lastgroup == expected.lastgroup


def test_lastindex_replay_preserves_endpos_and_is_thread_safe() -> None:
    pattern_text = r"(?P<outer>a(?P<inner>b))c?"
    expected = re.compile(pattern_text).search("abcx", 0, 3)
    actual = pcre.compile(pattern_text, pcre.Flag.NO_JIT).search("abcx", endpos=3)
    assert expected is not None
    assert actual is not None

    def read_properties(_: int) -> tuple[int | None, str | None]:
        return actual.lastindex, actual.lastgroup

    with ThreadPoolExecutor(max_workers=8) as executor:
        observed = list(executor.map(read_properties, range(256)))
    assert observed == [(expected.lastindex, expected.lastgroup)] * 256


def test_lastindex_replay_code_initialization_is_shared_thread_safely() -> None:
    pattern = pcre.compile(r"(?P<outer>a(?P<inner>b))", pcre.Flag.NO_JIT)
    matches = [pattern.fullmatch("ab") for _ in range(128)]
    assert all(match is not None for match in matches)

    def read_distinct_match(match) -> tuple[int | None, str | None]:
        return match.lastindex, match.lastgroup

    with ThreadPoolExecutor(max_workers=16) as executor:
        observed = list(executor.map(read_distinct_match, matches))
    assert observed == [(1, "outer")] * len(matches)
