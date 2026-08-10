# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import concurrent.futures
import re

import pytest

import pcre
from pcre import pcre as pcre_mod


@pytest.mark.parametrize(
    ("pattern", "subject", "replacement"),
    [
        (r"(a)(b)?", "a ab", r"[\1]"),
        (r"(a)(b)?", "a ab", r"[\g<0>]"),
        (r"(a)(b)?", "a ab", r"[\g<02>]"),
        (r"(?P<word>a)(?P<tail>b)?", "a ab", r"[\g<word>]"),
        (r"(?P<word>é)", "é é", "前\\g<word>後"),
        (b"(a)(b)?", b"a ab", rb"[\1]"),
        (b"(?P<word>a)", b"a a", rb"[\g<word>]"),
    ],
)
def test_single_reference_subn_is_exact_and_call_local(
    pattern,
    subject,
    replacement,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    compiled = pcre.compile(pattern)
    expected = re.compile(pattern).subn(replacement, subject)
    pcre.clear_cache()
    monkeypatch.setattr(
        pcre_mod,
        "_cached_replacement_parts",
        lambda *args: pytest.fail("single replacement reached template cache"),
    )

    assert compiled.subn(replacement, subject) == expected
    assert pcre_mod._replacement_cache_size() == 0


@pytest.mark.parametrize(
    "replacement",
    [r"\1-\2", r"[\1]$", r"\n\1"],
)
def test_extended_replacements_stay_on_compatibility_parser(
    replacement: str,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    compiled = pcre.compile(r"(a)(b)")
    original = pcre_mod._cached_replacement_parts
    calls = 0

    def counted(*args):
        nonlocal calls
        calls += 1
        return original(*args)

    monkeypatch.setattr(pcre_mod, "_cached_replacement_parts", counted)
    assert compiled.sub(replacement, "ab") == re.sub(r"(a)(b)", replacement, "ab")
    assert calls == 1


def test_single_count_is_call_local_and_replacement_subclass_stays_compatible(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class Replacement(str):
        pass

    compiled = pcre.compile(r"(a)")
    original = pcre_mod._cached_replacement_parts
    calls = 0

    def counted(*args):
        nonlocal calls
        calls += 1
        return original(*args)

    monkeypatch.setattr(pcre_mod, "_cached_replacement_parts", counted)
    assert compiled.sub(r"[\1]", "aa", count=1) == "[a]a"
    assert compiled.sub(Replacement(r"[\1]"), "a") == "[a]"
    assert calls == 0


def test_single_reference_subn_is_safe_on_shared_pattern() -> None:
    compiled = pcre.compile(r"(?P<word>é)")

    def replace_many() -> None:
        for _ in range(5_000):
            assert compiled.subn("前\\g<word>後", "é é") == ("前é後 前é後", 2)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(lambda _: replace_many(), range(8)))


@pytest.mark.parametrize("subject", ["a", "b"])
def test_duplicate_named_substitution_selects_participating_capture(
    subject: str,
) -> None:
    compiled = pcre.compile(r"(?J)(?P<word>a)|(?P<word>b)")

    assert compiled.subn(r"[\g<word>]", subject) == (f"[{subject}]", 1)


def test_single_reference_translator_rejects_ambiguous_inputs() -> None:
    compiled = pcre.compile(r"(?P<word>a)(b)")
    rejected = [
        "literal",
        "$\\1",
        "\\",
        "\\3",
        "\\12",
        "\\1\\2",
        "\\q",
        "\\g<word",
        "\\g<word>\\1",
        "\\g<>",
        "\\g<99999999999>",
        "\\g<3>",
        "\\g<未知>",
        "\\g<missing>",
    ]
    for replacement in rejected:
        assert (
            compiled._pattern._substitute_python_fast("ab", replacement)
            is NotImplemented
        )

    assert (
        compiled._pattern._substitute_python_fast(b"ab", b"\\g<\xff>") is NotImplemented
    )

    with pytest.raises(TypeError):
        compiled._pattern._substitute_python_fast("ab")
