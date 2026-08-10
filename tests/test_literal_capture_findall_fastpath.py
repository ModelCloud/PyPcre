from __future__ import annotations

import concurrent.futures
import re

import pytest

import pcre
from pcre import pcre as pcre_module


@pytest.mark.parametrize(
    ("literal", "subject"),
    [
        ("x", "xx xy x"),
        ("token", "token,token;not-token"),
        ("é", "é é éé"),
        ("雪", "雪雨雪"),
        ("a-b", "a-ba-b--a-b"),
        (b"x", b"xx xy x"),
        (b"token", b"token,token;not-token"),
        ("é".encode(), "é e éé".encode()),
    ],
)
def test_plain_capture_findall_matches_stdlib(literal, subject):
    opening = "(" if isinstance(literal, str) else b"("
    closing = ")" if isinstance(literal, str) else b")"
    pattern = opening + literal + closing
    assert pcre.compile(pattern).findall(subject) == re.compile(pattern).findall(
        subject
    )


@pytest.mark.parametrize(
    "pattern",
    [
        r"()",
        r"(x+)",
        r"(x|y)",
        r"(\.)",
        r"((x))",
        r"(?P<word>x)",
        rb"()",
        rb"(x+)",
        rb"(x|y)",
        rb"(\.)",
    ],
)
def test_nonliteral_capture_shapes_keep_native_findall(pattern):
    compiled = pcre.compile(pattern)
    assert compiled._literal_findall is None
    subject = b"x.xxy" if isinstance(pattern, bytes) else "x.xxy"
    assert compiled.findall(subject) == re.compile(pattern).findall(subject)


def test_literal_capture_metadata_has_strict_size_bound():
    accepted = "x" * 64
    rejected = "x" * 65
    assert pcre.compile(f"({accepted})")._literal_findall == accepted
    assert pcre.compile(f"({rejected})")._literal_findall is None


def test_literal_capture_fast_path_respects_nondefault_arguments():
    pattern = pcre.compile("(token)")
    subject = "token-token-token"
    assert pattern.findall(subject, pos=1) == re.compile("(token)").findall(subject, 1)
    assert pattern.findall(subject, endpos=9) == re.compile("(token)").findall(
        subject, 0, 9
    )
    assert pattern.findall(subject, options=pcre.Flag.NOTEMPTY) == [
        "token",
        "token",
        "token",
    ]
    assert pattern.findall(subject, pos=False) == ["token", "token", "token"]
    assert pattern.findall(subject, options=False) == ["token", "token", "token"]


def test_literal_capture_fast_path_excludes_options_and_subclasses():
    flagged = pcre.compile("(x)", pcre.Flag.CASELESS)
    assert flagged._literal_findall is None
    assert flagged.findall("xX") == ["x", "X"]

    class Text(str):
        pass

    plain = pcre.compile("(x)")
    assert plain.findall(Text("xxx")) == ["x", "x", "x"]

    class PatternSubclass(pcre.Pattern):
        pass

    wrapped = PatternSubclass(plain._pattern)
    assert wrapped.findall("xxx") == ["x", "x", "x"]


def test_literal_capture_metadata_stays_within_bounded_pattern_cache():
    original_limit = pcre.get_cache_limit()
    try:
        pcre.set_cache_limit(3)
        for index in range(40):
            compiled = pcre.compile(f"(literal{index})")
            assert compiled._literal_findall == f"literal{index}"
        cache = pcre_module._DEFAULT_COMPILE_LOCAL.cache
        assert len(cache) <= 3
        assert (
            sum(len(item._literal_findall or "") for item in cache.values()) <= 3 * 64
        )
    finally:
        pcre.set_cache_limit(original_limit)
        pcre.clear_cache()


def test_literal_capture_findall_is_thread_safe_on_shared_pattern():
    pattern = pcre.compile("(token)")
    subjects = ["token," * (index % 32) for index in range(512)]

    def exercise(subject: str):
        return pattern.findall(subject)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(exercise, subjects))
    assert results == [["token"] * (index % 32) for index in range(512)]
