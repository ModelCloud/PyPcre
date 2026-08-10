from __future__ import annotations

import concurrent.futures
import random
import re

import pytest

import pcre
from pcre import pcre as pcre_module


@pytest.mark.parametrize(
    ("groups", "subject"),
    [
        (("a", "b"), "zababx"),
        (("token", "-", "id"), "token-id token-id"),
        (("é", "雪"), "é雪xé雪"),
        (("a-b", "é", "雪"), "a-bé雪a-bé雪"),
        ((b"a", b"b"), b"zababx"),
        ((b"token", b"-", b"id"), b"token-id token-id"),
        (("é".encode(), "雪".encode()), "é雪xé雪".encode()),
    ],
)
def test_adjacent_literal_capture_findall_matches_stdlib(groups, subject):
    opening = "(" if isinstance(groups[0], str) else b"("
    closing = ")" if isinstance(groups[0], str) else b")"
    source = ("" if isinstance(opening, str) else b"").join(
        opening + group + closing for group in groups
    )
    compiled = pcre.compile(source)
    assert compiled._literal_findall_multi == (
        ("" if isinstance(opening, str) else b"").join(groups),
        groups,
    )
    assert compiled.findall(subject) == re.compile(source).findall(subject)


def test_adjacent_literal_capture_metadata_has_strict_bounds():
    eight_groups = "(a)" * 8
    assert pcre.compile(eight_groups)._literal_findall_multi is not None
    assert pcre.compile("(a)" * 9)._literal_findall_multi is None

    accepted = f"({'x' * 32})({'y' * 32})"
    rejected = f"({'x' * 32})({'y' * 33})"
    assert pcre.compile(accepted)._literal_findall_multi is not None
    assert pcre.compile(rejected)._literal_findall_multi is None


@pytest.mark.parametrize(
    "source",
    [
        r"(a+)(b)",
        r"(a)(b|c)",
        r"((a))(b)",
        r"(?P<a>a)(b)",
        r"(a)x(b)",
        r"(a)(b)$",
        rb"(a+)(b)",
        rb"(a)(b|c)",
    ],
)
def test_nonliteral_multi_capture_shapes_keep_native_findall(source):
    compiled = pcre.compile(source)
    assert compiled._literal_findall_multi is None
    subject = b"abacabc" if isinstance(source, bytes) else "abacabc"
    assert compiled.findall(subject) == re.compile(source).findall(subject)


def test_multi_capture_fast_path_respects_nondefault_arguments():
    compiled = pcre.compile("(a)(b)")
    stdlib = re.compile("(a)(b)")
    subject = "ababab"
    assert compiled.findall(subject, pos=1) == stdlib.findall(subject, 1)
    assert compiled.findall(subject, endpos=5) == stdlib.findall(subject, 0, 5)
    assert compiled.findall(subject, options=pcre.Flag.NOTEMPTY) == stdlib.findall(
        subject
    )
    assert compiled.findall(subject, pos=False) == stdlib.findall(subject)
    assert compiled.findall(subject, options=False) == stdlib.findall(subject)


def test_multi_capture_fast_path_excludes_flags_and_subclasses():
    flagged = pcre.compile("(a)(b)", pcre.Flag.CASELESS)
    assert flagged._literal_findall_multi is None
    assert flagged.findall("abAB") == [("a", "b"), ("A", "B")]

    class Text(str):
        pass

    plain = pcre.compile("(a)(b)")
    assert plain.findall(Text("abab")) == [("a", "b"), ("a", "b")]

    class PatternSubclass(pcre.Pattern):
        pass

    wrapped = PatternSubclass(plain._pattern)
    assert wrapped.findall("abab") == [("a", "b"), ("a", "b")]


def test_multi_capture_metadata_stays_within_bounded_pattern_cache():
    original_limit = pcre.get_cache_limit()
    try:
        pcre.set_cache_limit(3)
        for index in range(40):
            compiled = pcre.compile(f"(literal{index})(suffix)")
            assert compiled._literal_findall_multi is not None
        cache = pcre_module._DEFAULT_COMPILE_LOCAL.cache
        assert len(cache) <= 3
        retained_units = 0
        for item in cache.values():
            descriptor = item._literal_findall_multi
            assert descriptor is not None
            retained_units += len(descriptor[0]) + sum(map(len, descriptor[1]))
        assert retained_units <= 3 * 128
    finally:
        pcre.set_cache_limit(original_limit)
        pcre.clear_cache()


def test_multi_capture_findall_randomized_parity():
    generator = random.Random(0xF1ADA11)
    alphabet = "abcé雪"
    for _ in range(3_000):
        groups = tuple(
            "".join(generator.choices(alphabet, k=generator.randrange(1, 6)))
            for _ in range(generator.randrange(2, 9))
        )
        source = "".join(f"({group})" for group in groups)
        subject = "".join(generator.choices(alphabet, k=generator.randrange(96)))
        assert pcre.compile(source).findall(subject) == re.compile(source).findall(
            subject
        )


def test_multi_capture_findall_is_thread_safe_on_shared_pattern():
    pattern = pcre.compile("(token)(-)(id)")
    subjects = ["token-id," * (index % 32) for index in range(512)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(pattern.findall, subjects))

    expected_group = ("token", "-", "id")
    assert results == [[expected_group] * (index % 32) for index in range(512)]
