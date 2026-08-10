from __future__ import annotations

import concurrent.futures
import random
import re
import sys

import pytest

import pcre


@pytest.mark.parametrize(
    ("groups", "subject"),
    [
        (("a", "b"), "ababtailab"),
        (("token", "-", "id"), "token-idtoken-idtail"),
        (("é", "雪"), "é雪xé雪é雪"),
        (("a-b", "é", "雪"), "a-bé雪a-bé雪"),
        ((b"a", b"b"), b"ababtailab"),
        ((b"token", b"-", b"id"), b"token-idtoken-idtail"),
        (("é".encode(), "雪".encode()), "é雪xé雪é雪".encode()),
    ],
)
@pytest.mark.parametrize("maxsplit", [-8, -1, 0, 1, 2, 8])
def test_adjacent_literal_capture_split_matches_stdlib(groups, subject, maxsplit):
    opening = "(" if isinstance(groups[0], str) else b"("
    closing = ")" if isinstance(groups[0], str) else b")"
    empty = "" if isinstance(opening, str) else b""
    source = empty.join(opening + group + closing for group in groups)
    assert pcre.compile(source).split(subject, maxsplit) == re.compile(source).split(
        subject, maxsplit
    )


def test_multi_capture_split_excludes_flags_and_subclasses():
    flagged = pcre.compile("(a)(b)", pcre.Flag.CASELESS)
    assert flagged._literal_findall_multi is None
    assert flagged.split("abAB") == ["", "a", "b", "", "A", "B", ""]

    class Text(str):
        pass

    plain = pcre.compile("(a)(b)")
    subject = Text("abab")
    assert plain.split(subject) == re.compile("(a)(b)").split(subject)

    class PatternSubclass(pcre.Pattern):
        pass

    wrapped = PatternSubclass(plain._pattern)
    assert wrapped.split("abab") == re.compile("(a)(b)").split("abab")


def test_multi_capture_split_private_entry_rejects_invalid_shapes():
    backend = pcre.compile("(a)(b)")._pattern
    with pytest.raises(TypeError, match="exactly 4 positional"):
        backend._split_literal_captures_fast("ab", "ab", ("a", "b"))
    assert (
        backend._split_literal_captures_fast("ab", b"ab", ("a", "b"), 0)
        is NotImplemented
    )
    assert (
        backend._split_literal_captures_fast("ab", "ab", ["a", "b"], 0)
        is NotImplemented
    )
    assert backend._split_literal_captures_fast("ab", "ab", ("a",), 0) is NotImplemented
    assert (
        backend._split_literal_captures_fast("ab", "ab", ("a", b"b"), 0)
        is NotImplemented
    )
    with pytest.raises(OverflowError):
        backend._split_literal_captures_fast("ab", "ab", ("a", "b"), 10**100)


def test_multi_capture_split_randomized_parity():
    generator = random.Random(0x5A117A11)
    alphabet = "abcé雪"
    for _ in range(3_000):
        groups = tuple(
            "".join(generator.choices(alphabet, k=generator.randrange(1, 6)))
            for _ in range(generator.randrange(2, 9))
        )
        source = "".join(f"({group})" for group in groups)
        subject = "".join(generator.choices(alphabet, k=generator.randrange(96)))
        maxsplit = generator.randrange(-3, 9)
        assert pcre.compile(source).split(subject, maxsplit) == re.compile(
            source
        ).split(subject, maxsplit)


def test_multi_capture_split_is_thread_safe_on_shared_pattern():
    pattern = pcre.compile("(token)(-)(id)")
    stdlib = re.compile("(token)(-)(id)")
    subjects = ["token-id," * (index % 32) for index in range(512)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(pattern.split, subjects))

    assert results == [stdlib.split(subject) for subject in subjects]


def test_multi_capture_split_releases_all_result_references():
    pattern = pcre.compile("(literal-one)(literal-two)(literal-three)")
    descriptor = pattern._literal_findall_multi
    assert descriptor is not None
    groups = descriptor[1]
    baseline = tuple(sys.getrefcount(group) for group in groups)

    for _ in range(2_000):
        result = pattern.split("literal-oneliteral-twoliteral-three," * 16)
        del result

    assert tuple(sys.getrefcount(group) for group in groups) == baseline
