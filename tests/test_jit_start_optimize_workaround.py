# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""JIT start-optimization workaround (PCRE2 10.46/10.47).

Those releases' JIT fast-forward (start-of-match optimization) skips valid
start positions for patterns beginning with an optional atom + literal +
bounded class: pcre2_jit_match finds nothing for 0?a[b ]{0,2} on " ab "
while pcre2_match returns (1, 4).  Fixed upstream after 10.47.

The extension probes the loaded runtime once (_jit_start_optimize_broken)
and, when broken, compiles JIT-bound patterns with PCRE2_NO_START_OPTIMIZE.
These assertions must hold on EVERY runtime: on fixed PCRE2 versions the
probe reports 0 and the results are correct natively; on broken versions
the workaround makes them correct.
"""

from __future__ import annotations

import re

import pcre
import pcre_ext_c


# (pattern, subject, expected spans from re.finditer) — all reproduce the
# 10.46/10.47 JIT divergence without the workaround.
_CASES: list[tuple[str, str]] = [
    (r"0?a[b ]{0,2}", " ab "),
    (r"0?a[b ]{0,3}b?", " ab "),
    (r"0?a[g-k \s]{0,3}[g-k\s]?", " ka "),
    (r"d?1([a-f]{0,2}(?=[^_]))-", "1-0"),
    (r".c?d(?:2[g-k_]{0,2}[_]|b{0,2})", "_0dgc_0kb-bb21e22b22"),
    (r"(?s)(?i)[_ ]?b.{3,5}2+\w?", "di32s2 i13ce2sw\t- w\n-iw b20 2 5 2c"),
]


def test_probe_is_resolved() -> None:
    assert pcre_ext_c._jit_start_optimize_broken() in (0, 1)


def test_jit_matches_interpreter_and_re() -> None:
    for pattern, subject in _CASES:
        expected = [m.span() for m in re.finditer(pattern, subject)]

        jit_pat = pcre.compile(pattern)  # JIT on by default
        nojit_pat = pcre.compile(pattern, flags=pcre.Flag.NO_JIT)

        got_jit = [m.span() for m in jit_pat.finditer(subject)]
        got_int = [m.span() for m in nojit_pat.finditer(subject)]

        assert got_int == expected, (
            f"interpreter diverges from re: pattern={pattern!r} "
            f"interp={got_int} re={expected}"
        )
        assert got_jit == expected, (
            f"JIT result wrong (start-optimize workaround ineffective or "
            f"regressed): pattern={pattern!r} jit={got_jit} re={expected} "
            f"probe={pcre_ext_c._jit_start_optimize_broken()}"
        )

        for method in ("search", "match", "fullmatch"):
            want = getattr(re.compile(pattern), method)(subject)
            got = getattr(jit_pat, method)(subject)
            assert (got and got.span()) == (want and want.span()), (
                f"{method} diverges under JIT: pattern={pattern!r} "
                f"got={got and got.span()} want={want and want.span()}"
            )


def test_workaround_does_not_leak_into_flags() -> None:
    # The injected PCRE2_NO_START_OPTIMIZE bit is an internal workaround
    # detail; it must not appear in the pattern's public flags.
    compiled = pcre.compile(r"0?a[b ]{0,2}")
    assert not compiled.flags & pcre.Flag.NO_START_OPTIMIZE

    # A caller-requested NO_START_OPTIMIZE must still be reflected.
    explicit = pcre.compile(r"0?a[b ]{0,2}", flags=pcre.Flag.NO_START_OPTIMIZE)
    assert explicit.flags & pcre.Flag.NO_START_OPTIMIZE
    assert [m.span() for m in explicit.finditer(" ab ")] == [(1, 4)]
