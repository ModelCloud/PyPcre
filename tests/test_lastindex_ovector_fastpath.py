from __future__ import annotations

import concurrent.futures

import pytest

import pcre


@pytest.mark.parametrize(
    ("pattern", "subject", "expected_index", "expected_group"),
    [
        (r"(x)", "x", 1, None),
        (r"(x)?y", "y", None, None),
        (r"(a)|(b)|(c)", "b", 2, None),
        (r"(?:(a)|(?:(b)|(c)))", "c", 3, None),
        (r"(?P<only>x)", "x", 1, "only"),
    ],
)
def test_zero_or_one_participating_capture_has_exact_lastindex(
    pattern, subject, expected_index, expected_group
):
    match = pcre.fullmatch(pattern, subject)
    assert match is not None
    assert match.lastindex == expected_index
    assert match.lastgroup == expected_group


@pytest.mark.parametrize(
    ("pattern", "subject", "expected_index", "expected_group"),
    [
        (r"((a)|(b))", "a", 1, None),
        (r"(?P<outer>a(?P<inner>b))", "ab", 1, "outer"),
        (r"(?=(?P<look>a))(?P<body>a)", "a", 2, "body"),
    ],
)
def test_multiple_participants_keep_exact_replay_semantics(
    pattern, subject, expected_index, expected_group
):
    match = pcre.fullmatch(pattern, subject)
    assert match is not None
    assert match.lastindex == expected_index
    assert match.lastgroup == expected_group


def test_sole_duplicate_name_capture_resolves_lastgroup():
    pattern = pcre.compile(r"(?J)(?<word>a)|(?<word>b)", pcre.Flag.NO_JIT)
    first = pattern.fullmatch("a")
    second = pattern.fullmatch("b")
    assert first is not None
    assert second is not None
    assert (first.lastindex, first.lastgroup) == (1, "word")
    assert (second.lastindex, second.lastgroup) == (2, "word")


def test_shared_match_lastindex_fast_path_is_thread_safe():
    match = pcre.compile(r"(a)|(b)|(c)").fullmatch("b")
    assert match is not None

    def read(_: int):
        return match.lastindex, match.lastgroup

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        assert list(executor.map(read, range(1024))) == [(2, None)] * 1024


def test_distinct_match_lastindex_fast_path_is_thread_safe():
    pattern = pcre.compile(r"(?P<a>a)|(?P<b>b)|(?P<c>c)")
    subjects = ("a", "b", "c")

    def match_and_read(index: int):
        match = pattern.fullmatch(subjects[index % len(subjects)])
        assert match is not None
        return match.lastindex, match.lastgroup

    expected = [((index % 3) + 1, subjects[index % 3]) for index in range(768)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        assert list(executor.map(match_and_read, range(768))) == expected
