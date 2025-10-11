"""Regression tests for memory safety around PCRE error paths."""

from __future__ import annotations

import gc

import pcre
import pytest
from pcre import Flag


_BAD_OPTION_SENTINEL = 0xFFFFFFFF


@pytest.fixture(autouse=True)
def _reset_backends():
    """Ensure caches do not leak state between tests."""

    pcre.clear_cache()
    try:
        yield
    finally:
        pcre.clear_cache()


@pytest.mark.parametrize("flags", [Flag.NO_JIT, Flag.JIT], ids=["no_jit", "jit"])
def test_compile_error_loop_keeps_compiler_stable(flags: Flag) -> None:
    failing_pattern = "("
    expected_error = getattr(pcre, "PcreErrorMissingClosingParenthesis", pcre.PcreError)

    for _ in range(32):
        with pytest.raises(expected_error) as info:
            pcre.compile(failing_pattern, flags=flags)
        assert info.value.args and info.value.args[0] == "compile"

    compiled = pcre.compile("a+", flags=flags)
    match = compiled.match("aaa")
    assert match is not None
    assert match.group(0) == "aaa"


def test_jit_compile_error_does_not_poison_future_compiles() -> None:
    failing_pattern = "\\C"

    first_error: pcre.PcreError | None = None
    try:
        pcre.compile(failing_pattern, flags=Flag.JIT)
    except pcre.PcreError as exc:  # capture the first failure to pin expectations
        first_error = exc
    else:
        pytest.skip("backend accepted \\C with JIT enabled; cannot exercise jit error path")

    assert first_error is not None
    expected_type = type(first_error)
    expected_macro = getattr(first_error, "macro", None)
    assert first_error.args and first_error.args[0] == "jit_compile"

    # Access captured error after a GC cycle to ensure it owns valid backing memory.
    macro_snapshot = expected_macro
    gc.collect()
    assert getattr(first_error, "macro", None) == macro_snapshot

    for _ in range(8):
        with pytest.raises(expected_type) as info:
            pcre.compile(failing_pattern, flags=Flag.JIT)
        assert info.value.args and info.value.args[0] == "jit_compile"
        assert getattr(info.value, "macro", None) == expected_macro

    compiled_no_jit = pcre.compile(failing_pattern, flags=Flag.NO_JIT)
    assert compiled_no_jit.match("A") is not None

    healthy = pcre.compile("a+", flags=Flag.JIT)
    assert healthy.jit is True
    assert healthy.match("aaa") is not None


@pytest.mark.parametrize("flags", [Flag.NO_JIT, Flag.JIT], ids=["no_jit", "jit"])
def test_execution_error_leaves_pattern_operational(flags: Flag) -> None:
    pattern = pcre.compile("a+", flags=flags)

    observed_macro = None
    for _ in range(16):
        with pytest.raises(pcre.PcreError) as info:
            pattern.match("aaa", options=_BAD_OPTION_SENTINEL)
        assert info.value.args and info.value.args[0] == "match"
        macro = getattr(info.value, "macro", None)
        if observed_macro is None:
            observed_macro = macro
        else:
            assert macro == observed_macro

    assert pattern.match("aaa") is not None

    pcre.clear_cache()
    refresher = pcre.compile("a+", flags=flags)
    again = refresher.match("aaa")
    assert again is not None
    assert again.group(0) == "aaa"

    if flags == Flag.JIT:
        alt = pcre.compile("b+", flags=flags)
        assert alt.jit is True
        assert alt.match("bbb") is not None
