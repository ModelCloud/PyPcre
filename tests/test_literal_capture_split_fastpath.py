from __future__ import annotations

import concurrent.futures
import random
import re

import pytest

import pcre


@pytest.mark.parametrize(
    ("literal", "subject"),
    [
        ("x", "xxx"),
        ("token", "tokentokentailtoken"),
        ("é", "éaéé"),
        ("雪", "雪雨雪雪"),
        ("a-b", "a-ba-b--a-b"),
        (b"x", b"xxx"),
        (b"token", b"tokentokentailtoken"),
        ("é".encode(), "éaéé".encode()),
    ],
)
@pytest.mark.parametrize("maxsplit", [-8, -1, 0, 1, 2, 8])
def test_literal_capture_split_matches_stdlib(literal, subject, maxsplit):
    opening = "(" if isinstance(literal, str) else b"("
    closing = ")" if isinstance(literal, str) else b")"
    source = opening + literal + closing
    assert pcre.compile(source).split(subject, maxsplit) == re.compile(source).split(
        subject, maxsplit
    )


@pytest.mark.parametrize("pattern", [r"()", r"(x+)", r"(x|y)", rb"()", rb"(x+)"])
def test_nonliteral_capture_shapes_keep_native_split(pattern):
    compiled = pcre.compile(pattern)
    assert compiled._literal_findall is None
    subject = b"xxy" if isinstance(pattern, bytes) else "xxy"
    assert compiled.split(subject) == re.compile(pattern).split(subject)


def test_literal_capture_split_excludes_flags_and_subclasses():
    flagged = pcre.compile("(x)", pcre.Flag.CASELESS)
    assert flagged._literal_findall is None
    assert flagged.split("xX") == ["", "x", "", "X", ""]

    class Text(str):
        pass

    plain = pcre.compile("(x)")
    subject = Text("x-x")
    assert plain.split(subject) == re.compile("(x)").split(subject)

    class PatternSubclass(pcre.Pattern):
        pass

    wrapped = PatternSubclass(plain._pattern)
    assert wrapped.split("x-x") == re.compile("(x)").split("x-x")


def test_literal_capture_split_private_entry_rejects_invalid_shapes():
    backend = pcre.compile("(x)")._pattern
    with pytest.raises(TypeError, match="exactly 3 positional"):
        backend._split_literal_capture_fast("x", "x")
    assert backend._split_literal_capture_fast("x", b"x", 0) is NotImplemented
    assert (
        backend._split_literal_capture_fast(bytearray(b"x"), b"x", 0) is NotImplemented
    )
    with pytest.raises(OverflowError):
        backend._split_literal_capture_fast("x", "x", 10**100)


def test_literal_capture_split_randomized_parity():
    generator = random.Random(0x5A117)
    alphabet = "abcé雪"
    for _ in range(2_000):
        literal = "".join(generator.choices(alphabet, k=generator.randrange(1, 9)))
        subject = "".join(generator.choices(alphabet, k=generator.randrange(65)))
        maxsplit = generator.randrange(-3, 9)
        source = f"({literal})"
        assert pcre.compile(source).split(subject, maxsplit) == re.compile(
            source
        ).split(subject, maxsplit)


def test_literal_capture_split_is_thread_safe_on_shared_pattern():
    pattern = pcre.compile("(token)")
    subjects = ["token," * (index % 32) for index in range(512)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(pattern.split, subjects))

    assert results == [re.compile("(token)").split(subject) for subject in subjects]
