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


@pytest.mark.parametrize("template", [r"\12", r"\\1", r"\g<1>"])
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
    match = pcre.compile(r"(é)").fullmatch("é")
    assert match is not None

    def expand_many() -> None:
        for _ in range(10_000):
            assert match.expand("前\\1後") == "前é後"

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
