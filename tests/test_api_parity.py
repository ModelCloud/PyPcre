# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import inspect
import re
from enum import IntFlag

import pcre
import pcre_ext_c
import pytest
from pcre import Flag


BACKEND = pcre_ext_c


def test_purge_aliases_clear_cache():
    assert pcre.purge is pcre.clear_cache


def test_error_aliases_and_escape():
    assert pcre.error is pcre.PcreError
    assert pcre.PatternError is pcre.PcreError
    assert issubclass(pcre.Flag, IntFlag)
    assert not hasattr(pcre, "RegexFlag")
    assert hasattr(pcre.Flag, "CASELESS")
    assert not hasattr(pcre, "PCRE2_CASELESS")
    assert not hasattr(pcre, "PCRE2_UTF")
    assert int(pcre.Flag.CASELESS) == getattr(BACKEND, "PCRE2_CASELESS")
    assert int(pcre.Flag.UTF) == getattr(BACKEND, "PCRE2_UTF")
    assert (pcre.Flag.CASELESS | pcre.Flag.UTF) == (
            getattr(BACKEND, "PCRE2_CASELESS") | getattr(BACKEND, "PCRE2_UTF")
    )
    assert pcre.escape("a+b") == re.escape("a+b")
    assert pcre.escape(b"a+b") == re.escape(b"a+b")


def test_stdlib_style_flag_aliases():
    assert pcre.IGNORECASE == pcre.Flag.CASELESS
    assert pcre.I == pcre.Flag.CASELESS
    assert pcre.MULTILINE == pcre.Flag.MULTILINE
    assert pcre.M == pcre.Flag.MULTILINE
    assert pcre.DOTALL == pcre.Flag.DOTALL
    assert pcre.S == pcre.Flag.DOTALL
    assert pcre.VERBOSE == pcre.Flag.EXTENDED
    assert pcre.X == pcre.Flag.EXTENDED
    ascii_alias = pcre.Flag.NO_UTF | pcre.Flag.NO_UCP
    assert pcre.ASCII == ascii_alias
    assert pcre.A == ascii_alias
    assert pcre.UNICODE == 0
    assert pcre.U == 0


def test_specific_compile_error_exposes_dedicated_exception():
    with pytest.raises(pcre.PcreErrorMissingClosingParenthesis) as info:
        pcre.compile("(")

    exc = info.value
    assert isinstance(exc, pcre.PcreError)
    assert exc.macro == "PCRE2_ERROR_MISSING_CLOSING_PARENTHESIS"
    expected = getattr(BACKEND, "PCRE2_ERROR_MISSING_CLOSING_PARENTHESIS")
    assert exc.code == expected
    assert exc.error_code is pcre.PcreErrorCode.MISSING_CLOSING_PARENTHESIS


def test_error_classes_and_enum_are_reexported():
    assert hasattr(pcre, "PcreErrorJitStacklimit")
    assert issubclass(pcre.PcreErrorJitStacklimit, pcre.PcreError)
    assert pcre.PcreErrorCode.JIT_STACKLIMIT.value == getattr(BACKEND, "PCRE2_ERROR_JIT_STACKLIMIT")


def test_compile_accepts_stdlib_regex_flags():
    compiled = pcre.compile(r"pattern", flags=re.RegexFlag.IGNORECASE | re.RegexFlag.DOTALL)
    assert isinstance(compiled, pcre.Pattern)
    assert compiled.flags & Flag.CASELESS
    assert compiled.flags & Flag.DOTALL

    combo = pcre.compile(r"pattern", flags=[Flag.CASELESS, re.RegexFlag.MULTILINE])
    assert isinstance(combo, pcre.Pattern)
    assert combo.flags & Flag.CASELESS
    assert combo.flags & Flag.MULTILINE


def test_compile_rejects_incompatible_stdlib_regex_flags():
    with pytest.raises(ValueError):
        pcre.compile(r"pattern", flags=re.RegexFlag.DEBUG)

    with pytest.raises(ValueError):
        pcre.compile(r"pattern", flags=[re.RegexFlag.IGNORECASE, re.RegexFlag.ASCII])


def test_split_matches_stdlib_for_common_pattern():
    pattern = r"[,;]"
    text = "spam,ham;eggs"
    assert pcre.split(pattern, text) == re.split(pattern, text)


def test_split_bytes_matches_stdlib():
    pattern = br"[:,]"
    data = b"foo:bar,baz"
    assert pcre.split(pattern, data) == re.split(pattern, data)


def test_module_sub_behaves_like_re_for_named_groups():
    pattern = r"(?P<word>\w+)"
    replacement = r"<\g<word>>"
    text = "alpha beta"
    assert pcre.sub(pattern, replacement, text) == re.sub(pattern, replacement, text)


def test_module_subn_bytes_equivalence():
    pattern = br"(?P<byte>\w)"
    replacement = br"<\g<byte>>"
    data = b"ab"
    assert pcre.sub(pattern, replacement, data) == re.sub(pattern, replacement, data)
    assert pcre.subn(pattern, replacement, data, count=1) == re.subn(pattern, replacement, data, count=1)


def test_subn_returns_result_and_count():
    result, count = pcre.subn(r"a", "-", "banana", count=2)
    expected, expected_count = re.subn(r"a", "-", "banana", count=2)
    assert result == expected
    assert count == expected_count


def test_sub_callable_receives_match_and_returns_value():
    pattern = pcre.compile(r"(\w+)")

    def repl(match: pcre.Match) -> str:
        return match.group(0).upper()

    assert pattern.sub(repl, "hello world") == "HELLO WORLD"


def test_sub_bytes_literal_and_callable():
    pattern = pcre.compile(br"(\w)")
    assert pattern.sub(br"[\1]", b"ab") == b"[a][b]"

    def repl(match: pcre.Match) -> bytes:
        return match.group(0).upper()

    assert pattern.sub(repl, b"ab") == b"AB"


def test_pattern_subn_matches_stdlib():
    pattern = pcre.compile(r"(\w+)")
    control = re.compile(r"(\w+)")
    assert pattern.subn("-", "hello", count=True) == control.subn("-", "hello", 1)


def test_pattern_groups_matches_re():
    pattern = pcre.compile(r"(a)(?P<word>b)")
    control = re.compile(r"(a)(?P<word>b)")
    assert pattern.groups == control.groups


def test_pattern_groups_updates_after_dynamic_match():
    pattern = pcre.compile(r"(a)(b)?")
    assert pattern.groups == 2
    pattern.search("ab")
    assert pattern.groups == 2


def test_pattern_groups_handles_patterns_without_captures():
    assert pcre.compile(r"literal").groups == 0
    assert pcre.compile(br"literal").groups == 0


def test_split_includes_captures():
    compiled = pcre.compile(r"(-)")
    control = re.compile(r"(-)")
    assert compiled.split("a-b-c") == control.split("a-b-c")


def test_split_respects_maxsplit():
    assert pcre.split(r"-", "a-b-c", maxsplit=1) == re.split(r"-", "a-b-c", maxsplit=1)


def test_zero_length_pattern_substitution():
    assert pcre.sub(r"", "-", "ab") == re.sub(r"", "-", "ab")


def test_invalid_group_reference_raises():
    with pytest.raises(pcre.PcreError):
        pcre.sub(r"(a)", r"\2", "aaa")


def test_replacement_type_mismatch_raises():
    with pytest.raises(TypeError):
        pcre.sub(r"a", 123, "aaa")

    with pytest.raises(TypeError):
        pcre.sub(br"a", "x", b"aaa")


def test_callable_must_return_matching_type():
    def bad_repl(match: pcre.Match):
        return 42

    pattern = pcre.compile(r"a")
    with pytest.raises(TypeError):
        pattern.sub(bad_repl, "aaa")


def test_sub_bytes_requires_bytes_replacement():
    with pytest.raises(TypeError):
        pcre.sub(br"a", b"x", "text")


def test_callable_return_type_checked_for_bytes():
    pattern = pcre.compile(br"a")

    def bad(match: pcre.Match) -> str:
        return "str"

    with pytest.raises(TypeError):
        pattern.sub(bad, b"aaa")


def test_split_count_parameter_accepts_bool():
    assert pcre.split(r"-", "a-b-c", maxsplit=True) == re.split(r"-", "a-b-c", maxsplit=True)


def test_split_count_invalid_type_raises():
    with pytest.raises(TypeError):
        pcre.split(r"-", "a-b", maxsplit=1.5)


def test_match_attributes_align_with_re():
    pattern = pcre.compile(r"(a)(?P<word>b)")
    match = pattern.search("xaby", pos=1, endpos=4)
    assert isinstance(match, pcre.Match)
    assert match.re is pattern
    assert match.string == "xaby"
    assert match.pos == 1
    assert match.endpos == 4
    assert match.lastindex == 2
    assert match.lastgroup == "word"
    assert match.regs == ((1, 3), (1, 2), (2, 3))
    assert match.expand(r"<\1-\g<word>>") == "<a-b>"


def test_match_attributes_bytes():
    pattern = pcre.compile(br"(a)(b)")
    match = pattern.search(b"zzabzz", pos=2)
    assert isinstance(match, pcre.Match)
    assert match.re is pattern
    assert match.string == b"zzabzz"
    assert match.pos == 2
    assert match.endpos == len(b"zzabzz")
    assert match.lastgroup is None
    assert match.regs == ((2, 4), (2, 3), (3, 4))
    assert match.expand(br"[\1\2]") == b"[ab]"


def _signature_fingerprint(func):
    signature = inspect.signature(func)
    return tuple((param.name, param.kind, param.default) for param in signature.parameters.values())


def test_stdlib_function_signatures_align_with_pcre():
    stdlib_functions = {
        name: getattr(re, name)
        for name in dir(re)
        if not name.startswith("_") and inspect.isroutine(getattr(re, name))
    }

    for name, std_callable in stdlib_functions.items():
        pcre_callable = getattr(pcre, name, None)
        assert pcre_callable is not None, f"pcre is missing stdlib helper {name!r}"
        assert inspect.isroutine(pcre_callable), f"pcre.{name} should be a function"

        std_sig = _signature_fingerprint(std_callable)
        pcre_sig = _signature_fingerprint(pcre_callable)
        assert (
            pcre_sig == std_sig
        ), f"Signature mismatch for {name!r}: pcre{pcre_sig!r} != re{std_sig!r}"
