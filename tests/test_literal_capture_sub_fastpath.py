from __future__ import annotations

import re

import pytest

import pcre


@pytest.mark.parametrize(
    ("literal", "subject", "replacement"),
    [
        ("x", "xx xy x", "Y"),
        ("token", "token,token;not-token", "[id]"),
        ("é", "é é éé", "雪"),
        ("雪", "雪雨雪", "x"),
        ("a-b", "a-ba-b--a-b", "!"),
        (b"x", b"xx xy x", b"Y"),
        (b"token", b"token,token;not-token", b"[id]"),
        ("é".encode(), "é e éé".encode(), "雪".encode()),
    ],
)
@pytest.mark.parametrize("count", [0, 1, 2, 5, -1, 99])
def test_literal_capture_substitution_matches_stdlib(
    literal,
    subject,
    replacement,
    count,
) -> None:
    opening = "(" if isinstance(literal, str) else b"("
    closing = ")" if isinstance(literal, str) else b")"
    source = opening + literal + closing
    expected = re.subn(source, replacement, subject, count=count)

    compiled = pcre.compile(source)
    assert compiled.subn(replacement, subject, count=count) == expected
    assert compiled.sub(replacement, subject, count=count) == expected[0]
    assert pcre.subn(source, replacement, subject, count=count) == expected
    assert pcre.sub(source, replacement, subject, count=count) == expected[0]


def test_literal_capture_substitution_excludes_templates_and_nonliteral_shapes() -> None:
    pattern = pcre.compile("(x)")
    assert pattern.sub(r"[\1]", "xxx") == re.sub(r"(x)", r"[\1]", "xxx")
    assert pattern.subn(r"[\g<1>]", "xxx") == re.subn(r"(x)", r"[\g<1>]", "xxx")
    assert pattern.sub("$1", "xxx") == re.sub(r"(x)", "$1", "xxx")

    nonliteral = pcre.compile("(x+)")
    assert nonliteral._literal_replace is None
    assert nonliteral.sub("Y", "xxx") == re.sub(r"(x+)", "Y", "xxx")


def test_literal_capture_substitution_excludes_subclasses() -> None:
    class Text(str):
        pass

    class PatternSubclass(pcre.Pattern):
        pass

    compiled = pcre.compile("(x)")
    assert compiled.sub("Y", Text("xxx")) == "YYY"
    assert compiled.sub(Text("Y"), "xxx") == "YYY"
    wrapped = PatternSubclass(compiled._pattern)
    assert wrapped.sub("Y", "xxx") == "YYY"
