# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import concurrent.futures
import re

import pytest

import pcre
from pcre import re_compat


@pytest.mark.parametrize(
    ("pattern", "subject", "template", "expected"),
    [
        (r"(a)", "a", r"\1", "a"),
        (r"(a)", "a", r"[\1]", "[a]"),
        (r"(é)", "é", "前\\1後", "前é後"),
        (r"(a)?(b)?", "a", r"[\2]", "[]"),
        (b"(a)", b"a", rb"\1", b"a"),
        (b"(a)", b"a", rb"[\1]", b"[a]"),
        (b"(a)?(b)?", b"a", rb"[\2]", b"[]"),
        (r"(a)", "a", r"\g<0>", "a"),
        (r"(a)", "a", r"[\g<1>]", "[a]"),
        (r"(é)", "é", "前\\g<1>後", "前é後"),
        (b"(a)", b"a", rb"\g<0>", b"a"),
        (b"(a)", b"a", rb"[\g<1>]", b"[a]"),
        (
            "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)",
            "abcdefghijkl",
            r"[\g<12>]",
            "[l]",
        ),
        (r"(a)", "a", r"[\g<001>]", "[a]"),
        (r"(?P<word>a)", "a", r"[\g<word>]", "[a]"),
        (r"(?P<word>é)", "é", "前\\g<word>後", "前é後"),
        (r"(?P<word>a)?(?P<other>b)?", "a", r"[\g<other>]", "[]"),
        (b"(?P<word>a)", b"a", rb"[\g<word>]", b"[a]"),
    ],
)
def test_single_numeric_expand_is_exact_and_call_local(
    pattern,
    subject,
    template,
    expected,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    match = pcre.compile(pattern).fullmatch(subject)
    assert match is not None
    re_compat._cached_expand_template.cache_clear()
    monkeypatch.setattr(
        re_compat,
        "expand_match_template",
        lambda *args: pytest.fail("simple numeric expansion reached Python parser"),
    )

    assert match.expand(template) == expected
    assert re_compat._expand_template_cache_size() == 0


@pytest.mark.parametrize(
    "template",
    [
        r"\12",
        r"\\1",
        r"\g<name>",
        r"\g<999999999999999999999999999>",
        r"\g<13>",
    ],
)
def test_ambiguous_or_extended_expand_stays_on_compatibility_parser(
    template: str,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    match = pcre.compile("(a)" * 12).fullmatch("a" * 12)
    assert match is not None
    sentinel = object()
    monkeypatch.setattr(
        re_compat,
        "expand_match_template",
        lambda *args: sentinel,
    )

    assert match.expand(template) is sentinel


def test_single_numeric_expand_is_safe_on_one_match_across_threads() -> None:
    match = pcre.compile(r"(?P<word>é)").fullmatch("é")
    assert match is not None

    def expand_many() -> None:
        for _ in range(10_000):
            assert match.expand("前\\1後") == "前é後"
            assert match.expand("前\\g<word>後") == "前é後"
            assert match.expand("前\\1中\\g<word>後") == "前é中é後"

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(lambda _: expand_many(), range(8)))


@pytest.mark.parametrize(
    ("pattern", "subject"),
    [
        (r"(a)?(b)?", "a"),
        (r"(é)?(β)?", "éβ"),
        (b"(a)?(b)?", b"a"),
    ],
)
def test_numeric_expand_differential_matrix(pattern, subject) -> None:
    expected_match = re.fullmatch(pattern, subject)
    actual_match = pcre.fullmatch(pattern, subject)
    assert expected_match is not None
    assert actual_match is not None

    prefixes = ("", "[", "前", "$", "12")
    suffixes = ("", "]", "後", "$", r"\g<2>")
    for group in (1, 2):
        for prefix in prefixes:
            for suffix in suffixes:
                template = f"{prefix}\\{group}{suffix}"
                if isinstance(pattern, bytes):
                    template = template.encode()
                assert actual_match.expand(template) == expected_match.expand(template)


@pytest.mark.parametrize("subject", ["a", "b"])
def test_duplicate_named_expand_selects_the_participating_capture(subject: str) -> None:
    match = pcre.fullmatch(r"(?J)(?P<word>a)|(?P<word>b)", subject)
    assert match is not None

    assert match.expand(r"[\g<word>]") == f"[{subject}]"


@pytest.mark.parametrize(
    ("pattern", "subject"),
    [
        (r"(?P<first>a)?(?P<second>b)?", "a"),
        (r"(?P<first>é)?(?P<second>β)?", "éβ"),
        (b"(?P<first>a)?(?P<second>b)?", b"a"),
    ],
)
def test_named_expand_differential_matrix(pattern, subject) -> None:
    expected_match = re.fullmatch(pattern, subject)
    actual_match = pcre.fullmatch(pattern, subject)
    assert expected_match is not None
    assert actual_match is not None

    for name in ("first", "second"):
        for prefix, suffix in (("", ""), ("[", "]"), ("前", "後")):
            template = f"{prefix}\\g<{name}>{suffix}"
            if isinstance(pattern, bytes):
                template = template.encode()
            assert actual_match.expand(template) == expected_match.expand(template)


@pytest.mark.parametrize(
    ("pattern", "subject", "template", "expected"),
    [
        (r"(a)(b)", "ab", r"[\1]-\2", "[a]-b"),
        (r"(?P<a>a)(?P<b>b)", "ab", r"[\g<a>]-\g<b>", "[a]-b"),
        (r"(?P<a>a)", "a", r"\g<0>:\g<a>", "a:a"),
        (r"(é)(β)", "éβ", "前\\1中\\g<2>後", "前é中β後"),
        (r"(a)?(b)?", "a", r"[\1]-[\2]", "[a]-[]"),
        (b"(a)(b)", b"ab", rb"[\1]-\g<2>", b"[a]-b"),
        (
            b"(?P<a>a)(?P<b>b)",
            b"ab",
            rb"[\g<a>]-\g<b>",
            b"[a]-b",
        ),
    ],
)
def test_two_reference_expand_is_exact_and_call_local(
    pattern,
    subject,
    template,
    expected,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    match = pcre.fullmatch(pattern, subject)
    assert match is not None
    re_compat._cached_expand_template.cache_clear()
    monkeypatch.setattr(
        re_compat,
        "expand_match_template",
        lambda *args: pytest.fail("two-reference expansion reached Python parser"),
    )

    assert match.expand(template) == expected
    assert re_compat._expand_template_cache_size() == 0


def test_three_references_stay_on_compatibility_parser(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    match = pcre.fullmatch(r"(a)(b)(c)", "abc")
    assert match is not None
    sentinel = object()
    monkeypatch.setattr(re_compat, "expand_match_template", lambda *args: sentinel)

    assert match.expand(r"\1\2\3") is sentinel


@pytest.mark.parametrize(
    ("pattern", "subject"),
    [
        (r"(?P<first>a)?(?P<second>b)?", "a"),
        (r"(?P<first>é)?(?P<second>β)?", "éβ"),
        (b"(?P<first>a)?(?P<second>b)?", b"a"),
    ],
)
def test_two_reference_expand_differential_matrix(pattern, subject) -> None:
    expected_match = re.fullmatch(pattern, subject)
    actual_match = pcre.fullmatch(pattern, subject)
    assert expected_match is not None
    assert actual_match is not None

    references = (r"\1", r"\2", r"\g<0>", r"\g<01>", r"\g<first>", r"\g<second>")
    literals = (("", "", ""), ("[", "]: [", "]"), ("前", "中", "後"))
    for first in references:
        for second in references:
            for prefix, middle, suffix in literals:
                template = f"{prefix}{first}{middle}{second}{suffix}"
                if isinstance(pattern, bytes):
                    template = template.encode()
                assert actual_match.expand(template) == expected_match.expand(template)
