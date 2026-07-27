# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Regression tests for JIT anchoring correctness.

Some PCRE2 builds' pcre2_jit_match() silently ignores PCRE2_ANCHORED and
PCRE2_ENDANCHORED when passed as match-time options, which previously let
Pattern.match()/fullmatch() report matches at the wrong position (or ones
that don't reach the required end) whenever the pattern lacked a literal
first character for the has_first_literal fast path to filter first.
"""

import pcre_ext_c
import pytest


def _compile(pattern, *, jit):
    return pcre_ext_c.compile(pattern, jit=jit)


@pytest.mark.parametrize("jit", [True, False], ids=["jit", "nojit"])
class TestMatchAnchoring:
    def test_match_rejects_non_start_occurrence(self, jit):
        # No literal first character, so the has_first_literal fast path
        # can't mask a broken ANCHORED option.
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        assert pattern.match(b"X2025-10-08") is None

    def test_match_accepts_start_occurrence(self, jit):
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        match = pattern.match(b"2025-10-08trailing")
        assert match is not None
        assert match.span() == (0, 10)

    def test_match_rejects_non_start_occurrence_str(self, jit):
        pattern = _compile(r"\d{4}-\d{2}-\d{2}", jit=jit)
        assert pattern.match("X2025-10-08") is None


@pytest.mark.parametrize("jit", [True, False], ids=["jit", "nojit"])
class TestFullmatchAnchoring:
    def test_fullmatch_exact_span(self, jit):
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        match = pattern.fullmatch(b"2025-10-08")
        assert match is not None
        assert match.span() == (0, 10)

    def test_fullmatch_rejects_trailing_content(self, jit):
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        assert pattern.fullmatch(b"2025-10-08X") is None

    def test_fullmatch_rejects_leading_content(self, jit):
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        assert pattern.fullmatch(b"X2025-10-08") is None

    def test_fullmatch_reexplores_alternation_to_reach_end(self, jit):
        # A naive engine that ignores end-anchoring would greedily accept
        # the leftmost alternative ("a") and stop; a correct fullmatch must
        # backtrack into the "ab" alternative to cover the whole subject.
        pattern = _compile(rb"a|ab", jit=jit)
        match = pattern.fullmatch(b"ab")
        assert match is not None
        assert match.span() == (0, 2)

    def test_fullmatch_short_alternative_still_works(self, jit):
        pattern = _compile(rb"a|ab", jit=jit)
        match = pattern.fullmatch(b"a")
        assert match is not None
        assert match.span() == (0, 1)

    def test_fullmatch_with_endpos_and_trailing_content(self, jit):
        # endpos sets the PCRE2 offset limit; the match must end at that
        # limit, not at the end of the full subject buffer.
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        match = pattern.fullmatch(b"2025-10-08X", endpos=10)
        assert match is not None
        assert match.span() == (0, 10)

    def test_fullmatch_with_endpos_rejects_overrun(self, jit):
        pattern = _compile(rb"\d{4}-\d{2}-\d{2}", jit=jit)
        assert pattern.fullmatch(b"2025-10-08XX", endpos=11) is None

    def test_fullmatch_endpos_respects_alternation(self, jit):
        # With a trailing character after the endpos, the fallback
        # pcre2_match() re-run must still anchor at endpos.
        pattern = _compile(rb"a|ab", jit=jit)
        match = pattern.fullmatch(b"abX", endpos=2)
        assert match is not None
        assert match.span() == (0, 2)
        match = pattern.fullmatch(b"abX", endpos=1)
        assert match is not None
        assert match.span() == (0, 1)


if __name__ == "__main__":
    import sys

    sys.exit(pytest.main([__file__]))
