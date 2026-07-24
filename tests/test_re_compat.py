# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Coverage tests for the public helpers in pcre.re_compat."""

from __future__ import annotations

from typing import Any

import pcre.re_compat as rc
import pytest


def test_prepare_subject_memoryview() -> None:
    data = b"hello"
    assert rc.prepare_subject(memoryview(data)) == data
    assert rc.prepare_subject("hello") == "hello"


def test_is_bytes_like() -> None:
    assert rc.is_bytes_like(b"x") is True
    assert rc.is_bytes_like(bytearray(b"x")) is True
    assert rc.is_bytes_like(memoryview(b"x")) is True
    assert rc.is_bytes_like("x") is False


def test_normalise_count() -> None:
    assert rc.normalise_count(3) == 3
    assert rc.normalise_count(0) is None
    assert rc.normalise_count(-1) is None
    assert rc.normalise_count(None) is None
    assert rc.normalise_count(True) == 1


def test_resolve_endpos() -> None:
    assert rc.resolve_endpos("hello", None) == 5
    assert rc.resolve_endpos("hello", -1) == 5
    assert rc.resolve_endpos("hello", 3) == 3


def test_compute_next_pos() -> None:
    assert rc.compute_next_pos(0, (1, 3), None) == 3
    assert rc.compute_next_pos(0, (2, 2), None) == 3
    assert rc.compute_next_pos(0, (2, 5), 4) == 5
    assert rc.compute_next_pos(0, (2, 5), 3) == 5


def test_coerce_group_value() -> None:
    assert rc.coerce_group_value("x", is_bytes=False, empty="") == "x"
    assert rc.coerce_group_value(None, is_bytes=False, empty="") == ""
    assert rc.coerce_group_value(b"x", is_bytes=True, empty=b"") == b"x"
    assert rc.coerce_group_value(bytearray(b"x"), is_bytes=True, empty=b"") == b"x"
    assert rc.coerce_group_value(memoryview(b"x"), is_bytes=True, empty=b"") == b"x"
    with pytest.raises(TypeError):
        rc.coerce_group_value("x", is_bytes=True, empty=b"")
    with pytest.raises(TypeError):
        rc.coerce_group_value(b"x", is_bytes=False, empty="")


def test_coerce_subject_slice() -> None:
    assert rc.coerce_subject_slice("hello", 1, 3, is_bytes=False) == "el"
    assert rc.coerce_subject_slice(b"hello", 1, 3, is_bytes=True) == b"el"
    assert rc.coerce_subject_slice(bytearray(b"hello"), 1, 3, is_bytes=True) == b"el"
    assert rc.coerce_subject_slice(memoryview(b"hello"), 1, 3, is_bytes=True) == b"el"


def test_normalise_replacement() -> None:
    assert rc.normalise_replacement("x", is_bytes=False) == "x"
    assert rc.normalise_replacement(b"x", is_bytes=True) == b"x"
    assert rc.normalise_replacement(bytearray(b"x"), is_bytes=True) == b"x"
    assert rc.normalise_replacement(memoryview(b"x"), is_bytes=True) == b"x"
    with pytest.raises(TypeError):
        rc.normalise_replacement("x", is_bytes=True)
    with pytest.raises(TypeError):
        rc.normalise_replacement(b"x", is_bytes=False)


def test_join_parts() -> None:
    assert rc.join_parts(["a", "b"], is_bytes=False) == "ab"
    assert rc.join_parts([b"a", b"b"], is_bytes=True) == b"ab"
    assert rc.join_parts([bytearray(b"a"), memoryview(b"b")], is_bytes=True) == b"ab"
    with pytest.raises(TypeError):
        rc.join_parts(["a"], is_bytes=True)


def test_render_template_tuple_format() -> None:
    """Python 3.11 returns (group_slots, literals); newer versions return a flat list."""

    class FakeMatch:
        def group(self, index: int) -> str:
            return {1: "X", 2: "YY"}.get(index, "")

    match = FakeMatch()
    # Tuple format: group_slots=[(1, 1)], literals=["pre", None, "post"]
    assert rc.render_template(([(1, 1)], ["pre", None, "post"]), match, is_bytes=False, empty="") == "preXpost"
    # Flat list format with group reference ints and literal strings.
    assert rc.render_template(["pre", 1, "post"], match, is_bytes=False, empty="") == "preXpost"


def test_expand_match_template_bytes() -> None:
    import pcre

    pattern = pcre.compile(br"(\w+)")
    match = pattern.search(b"hello world")
    assert match.expand(br"[\1]") == b"[hello]"


def test_count_capturing_groups_variants() -> None:
    assert rc.count_capturing_groups("(a)(b)") == 2
    assert rc.count_capturing_groups(b"(a)(b)") == 2
    assert rc.count_capturing_groups(bytearray(b"(a)(b)")) == 2
    assert rc.count_capturing_groups(memoryview(b"(a)(b)")) == 2
    # Character classes and escapes should not be counted as captures.
    assert rc.count_capturing_groups(r"[a(b)]") == 0
    assert rc.count_capturing_groups(r"a\(b") == 0
    assert rc.count_capturing_groups(r"(?P<n>a)(?:(?<=x)y)(a)") == 2
    assert rc.count_capturing_groups(r"(?>a)(?:b)(?=c)(?!d)(?<=e)(?<!f)(?#g)(*ACCEPT)") == 0
    assert rc.count_capturing_groups(r"(?P=n)(?P>name)") == 0
    assert rc.count_capturing_groups(r"(?P<name>a)(?P'name')(?<name2>b)(?'name3'c)") == 4


def test_is_capturing_group_start() -> None:
    assert rc.is_capturing_group_start("(a)", 0) is True
    assert rc.is_capturing_group_start("(?:a)", 0) is False
    assert rc.is_capturing_group_start("(?P<n>a)", 0) is True
    assert rc.is_capturing_group_start("(?P=n)", 0) is False
    assert rc.is_capturing_group_start("(?P>n)", 0) is False
    assert rc.is_capturing_group_start("(?|a|b)", 0) is True
    assert rc.is_capturing_group_start("(?=a)", 0) is False
    assert rc.is_capturing_group_start("(?!a)", 0) is False
    assert rc.is_capturing_group_start("(?<=a)", 0) is False
    assert rc.is_capturing_group_start("(?<!a)", 0) is False
    assert rc.is_capturing_group_start("(?#a)", 0) is False
    assert rc.is_capturing_group_start("(*ACCEPT)", 0) is False
    assert rc.is_capturing_group_start("(?<name>a)", 0) is True
    assert rc.is_capturing_group_start("(?<=a)", 0) is False
    assert rc.is_capturing_group_start("(?<!a)", 0) is False
    assert rc.is_capturing_group_start("(?i)", 0) is False


def test_maybe_infer_group_count() -> None:
    assert rc.maybe_infer_group_count(r"(a)(b)") == 2
    assert rc.maybe_infer_group_count(b"(a)(b)") == 2
    assert rc.maybe_infer_group_count(bytearray(b"(a)(b)")) == 2
    assert rc.maybe_infer_group_count(memoryview(b"(a)(b)")) == 2


class FakeMatch:
    def __init__(self, groups: tuple[Any, ...], pattern: Any, string: Any) -> None:
        self._groups = groups
        self.re = pattern
        self.string = string

    def group(self, *indices: Any) -> Any:
        if len(indices) == 1:
            return self._groups[indices[0]]
        return tuple(self._groups[i] for i in indices)

    def groups(self, default: Any = None) -> tuple[Any, ...]:
        return self._groups[1:]

    def groupdict(self, default: Any = None) -> dict[str, Any]:
        return {}

    def start(self, group: int = 0) -> int:
        return 0

    def end(self, group: int = 0) -> int:
        return 0

    def span(self, group: int = 0) -> tuple[int, int]:
        return (0, 0)

    def expand(self, template: str) -> str:
        return rc.expand_match_template(self, template)


def test_re_compat_match_object() -> None:
    pattern = type("Pattern", (), {"groupindex": {"word": 1}, "groups": 1})()
    match = rc.Match(pattern, FakeMatch(("full", "value"), pattern, "value"), "value", 0, 5)
    assert match.group(0) == "full"
    assert match.groups() == ("value",)
    assert match.groupdict() == {}
    assert match.start() == 0
    assert match.end() == 0
    assert match.span() == (0, 0)
    assert match[0] == "full"
    assert match.lastindex == 1
    assert match.lastgroup == "word"
    assert match.regs == ((0, 0), (0, 0))
    assert match.string == "value"
    assert match.pos == 0
    assert match.endpos == 5


def test_re_compat_match_lastindex_none() -> None:
    pattern = type("Pattern", (), {"groupindex": {}, "groups": 0})()
    match = rc.Match(pattern, FakeMatch(("full",), pattern, "x"), "x", 0, 1)
    assert match.lastindex is None
    assert match.lastgroup is None


def test_match_expand() -> None:
    pattern = type("Pattern", (), {"groupindex": {"word": 1}, "groups": 1})()
    match = rc.Match(pattern, FakeMatch(("full", "value"), pattern, "value"), "value", 0, 5)
    assert match.expand(r"[\1]") == "[value]"


def test_expand_match_template_type_errors() -> None:
    pattern = type("Pattern", (), {"groupindex": {}, "groups": 1})()
    text_match = rc.Match(pattern, FakeMatch(("hello", "world"), pattern, "hello world"), "hello world", 0, 11)
    bytes_match = rc.Match(pattern, FakeMatch((b"hello", b"world"), pattern, b"hello world"), b"hello world", 0, 11)

    with pytest.raises(TypeError):
        rc.expand_match_template(text_match, b"[\1]")
    with pytest.raises(TypeError):
        rc.expand_match_template(bytes_match, "[\\1]")


