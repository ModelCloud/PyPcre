# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
"""Regressions for GitHub issue #131.

Two independent defects are covered:

* ``Match.lastindex``/``lastgroup`` replayed the match from ``ovector[0]`` with
  an extra ``PCRE2_ANCHORED`` instead of from the original start offset, so a
  pattern using ``\\K`` (which moves the reported match start) or a match found
  after a forward scan replayed into a different (or no) match and raised from
  an attribute getter.
* A JIT stack overflow (``PCRE2_ERROR_JIT_STACKLIMIT``) escaped as
  ``PcreErrorJitStacklimit`` instead of falling back to the interpreter.
"""

from __future__ import annotations

import re
import subprocess
import sys
from contextlib import contextmanager

import pcre_ext_c as raw
import pytest

import pcre


@contextmanager
def _jit_stack_limits(start: int, maximum: int):
    original = raw.get_jit_stack_limits()
    try:
        raw.set_jit_stack_limits(start, maximum)
        raw.clear_jit_stack_cache()
        yield
    finally:
        raw.set_jit_stack_limits(*original)
        raw.clear_jit_stack_cache()


def test_lastindex_replay_uses_original_start_offset_for_keep() -> None:
    # ``\K`` resets the reported match start to 1 while the engine actually
    # started at offset 0; replaying anchored at ovector[0] cannot reproduce it.
    match = pcre.search(r"(a)\K(b)(c)", "abc")
    assert match is not None
    assert match.span() == (1, 3)
    assert match.lastindex == 3
    assert match.lastgroup is None
    # The interpreter (NO_JIT) path is the reference for pcre-only syntax.
    no_jit = pcre.search(r"(a)\K(b)(c)", "abc", pcre.Flag.NO_JIT)
    assert no_jit is not None
    assert (no_jit.lastindex, no_jit.lastgroup) == (3, None)


def test_lastindex_replay_uses_original_start_offset_after_scan() -> None:
    # The match is found at offset 1 after a failed attempt at 0.  Replaying
    # anchored at ovector[0] loses ``\G``'s "start of this attempt" semantics
    # because the extra PCRE2_ANCHORED changed the reported outcome.
    match = pcre.search(r"(?:\G(b)|b)(c)(d)", "xbcd")
    assert match is not None
    assert match.span() == (1, 4)
    assert match.lastindex == 3
    assert match.lastgroup is None


@pytest.mark.parametrize(
    "pattern,subject",
    [
        (r"(a)\K(b)(c)", "abc"),
        (r"(?:\G(b)|b)(c)(d)", "xbcd"),
    ],
)
def test_lastindex_replay_preserves_user_match_options(
    pattern: str,
    subject: str,
) -> None:
    # PCRE2_NOTEMPTY_ATSTART is a match-time option; the replay must carry it
    # (and the real start offset) instead of synthesising its own options.
    match = pcre.search(pattern, subject, pcre.Flag.NOTEMPTY_ATSTART)
    assert match is not None
    assert match.lastindex == 3
    no_jit = pcre.search(pattern, subject, pcre.Flag.NOTEMPTY_ATSTART | pcre.Flag.NO_JIT)
    assert no_jit is not None
    assert no_jit.lastindex == match.lastindex


def test_lastindex_falls_back_when_replay_pattern_is_too_large() -> None:
    # Recompiling with PCRE2_AUTO_CALLOUT overflows PCRE2's pattern size limit.
    # The getter must not raise; it degrades to the highest participating
    # group, which here agrees with stdlib ``re``.
    pattern = "(a)(b)" + "c" * 20000
    subject = "ab" + "c" * 20000
    expected = re.match(pattern, subject)
    assert expected is not None

    match = pcre.match(pattern, subject)
    assert match is not None
    assert match.lastindex == expected.lastindex == 2
    assert match.lastgroup is None
    # The fallback is cached, so a second read cannot raise either.
    assert match.lastindex == 2


@pytest.mark.parametrize(
    "pattern,subject",
    [
        (r"(a)(b)", "ab"),
        (r"(a)|(b)", "b"),
        (r"((a)|(b))", "a"),
        (r"(a)?(b)", "b"),
        (r"(?P<outer>a(?P<inner>b))c?", "abc"),
        (r"(\w)(\d)?", "a"),
        (r"((a)(b))?(c)", "c"),
    ],
)
@pytest.mark.parametrize("method", ["search", "match", "fullmatch"])
def test_lastindex_matches_stdlib_re_with_pos_and_endpos(
    pattern: str,
    subject: str,
    method: str,
) -> None:
    reference = re.compile(pattern)
    candidate = pcre.compile(pattern, pcre.Flag.NO_JIT)
    for pos in range(len(subject) + 1):
        for endpos in range(pos, len(subject) + 1):
            expected = getattr(reference, method)(subject, pos, endpos)
            actual = getattr(candidate, method)(subject, pos, endpos)
            if expected is None:
                assert actual is None, (pattern, subject, pos, endpos)
                continue
            assert actual is not None, (pattern, subject, pos, endpos)
            assert (actual.lastindex, actual.lastgroup) == (
                expected.lastindex,
                expected.lastgroup,
            ), (pattern, subject, pos, endpos)


def test_jit_stack_limit_falls_back_to_interpreter() -> None:
    subject = "ab" * 20000 + "c"
    expected = re.match(r"(a|b)*c", subject)
    assert expected is not None

    match = pcre.match(r"(a|b)*c", subject)
    assert match is not None
    assert match.span() == expected.span() == (0, 40001)
    assert match.lastindex == expected.lastindex == 1


def test_jit_stack_limit_fallback_is_exercised_with_small_stack() -> None:
    # A deliberately tiny JIT stack guarantees PCRE2_ERROR_JIT_STACKLIMIT; the
    # public call must still succeed through the interpreter retry.
    subject = "ab" * 20000 + "c"
    with _jit_stack_limits(32 * 1024, 64 * 1024):
        match = pcre.match(r"(a|b)*c", subject)
        assert match is not None
        assert match.span() == (0, 40001)
        assert match.lastindex == 1


def test_jit_stack_limit_fallback_covers_iteration_helpers() -> None:
    subject = "ab" * 20000 + "c"
    with _jit_stack_limits(32 * 1024, 64 * 1024):
        assert [m.span() for m in pcre.finditer(r"(?:a|b)*c", subject)] == [(0, 40001)]
        assert pcre.findall(r"(?:a|b)*c", subject) == [subject]
        assert pcre.split(r"(?:a|b)*c", subject) == ["", ""]
        assert pcre.sub(r"(?:a|b)*c", "-", subject) == "-"


def test_jit_stack_limit_fallback_covers_substitute() -> None:
    subject = "ab" * 20000 + "c"
    with _jit_stack_limits(32 * 1024, 64 * 1024):
        assert pcre.subn(r"(?:a|b)*c", "-", subject)[1] == 1


def test_default_jit_stack_max_is_raised() -> None:
    # Read the defaults from a fresh interpreter so other tests that tweak the
    # process-global limits cannot influence the assertion.
    completed = subprocess.run(
        [
            sys.executable,
            "-c",
            "import pcre_ext_c as raw; print(*raw.get_jit_stack_limits())",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    start, maximum = (int(part) for part in completed.stdout.split())
    assert start == 32 * 1024
    assert maximum >= 8 * 1024 * 1024
