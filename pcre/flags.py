# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Static definitions for PCRE2 option flags used by the Python bindings."""

from __future__ import annotations

from enum import IntFlag


class Flag(IntFlag):
    # Python only flags
    NO_UTF: int = 0x100000000
    """Python-only flag: Disable UTF defaults applied by the Python wrapper."""
    NO_UCP: int = 0x200000000
    """Python-only flag: Disable Unicode property defaults applied by the Python wrapper."""
    JIT: int = 0x400000000
    """Python-only flag: Request JIT compilation even when disabled globally."""
    NO_JIT: int = 0x800000000
    """Python-only flag: Force interpretive execution even when JIT is enabled."""
    THREADS: int = 0x1000000000
    """Python-only flag: Opt in to the threaded execution backend for this pattern."""
    NO_THREADS: int = 0x2000000000
    """Python-only flag: Force sequential execution even when threading is enabled."""
    COMPAT_UNICODE_ESCAPE: int = 0x4000000000
    """Python-only flag: Emulate re.escape semantics for \\u/\\U escapes in string patterns."""

    """Bit flags accepted by the Python PCRE2 bindings."""
    ANCHORED: int = 0x80000000
    """Compile: Force matches to start at the first code unit of the subject."""
    NO_UTF_CHECK: int = 0x40000000
    """Compile: Skip UTF validity checks; the caller guarantees pattern and subject integrity."""
    ENDANCHORED: int = 0x20000000
    """Compile: Require matches to extend to the final code unit of the subject."""
    ALLOW_EMPTY_CLASS: int = 0x00000001
    """Compile: Permit empty character classes ([]) to match the empty string.
Execute alias Flag.NOTBOL: Treat the current match position as not being at the start of a line."""
    ALT_BSUX: int = 0x00000002
    """Compile: Interpret \\u, \\U, and \\x using ECMAScript-style escape rules.
Execute alias Flag.NOTEOL: Treat the subject as not ending at the logical end of line."""
    AUTO_CALLOUT: int = 0x00000004
    """Compile: Automatically insert callouts numbered 255 before each pattern item.
Execute alias Flag.NOTEMPTY: Reject matches of the empty string."""
    CASELESS: int = 0x00000008
    """Compile: Enable case-insensitive matching.
Execute alias Flag.NOTEMPTY_ATSTART: Reject empty-string matches at the starting offset."""
    DOLLAR_ENDONLY: int = 0x00000010
    """Compile: Make $ match only at the absolute end, not before a trailing newline.
Execute alias Flag.PARTIAL_SOFT: Allow partial matches while continuing to search for full matches."""
    DOTALL: int = 0x00000020
    """Compile: Allow dot (.) to match newline characters.
Execute alias Flag.PARTIAL_HARD: Return partial matches immediately without continuing the scan."""
    DUPNAMES: int = 0x00000040
    """Compile: Allow multiple capture groups to share the same symbolic name.
Execute alias Flag.DFA_RESTART: Resume DFA matching after a partial match using saved state."""
    EXTENDED: int = 0x00000080
    """Compile: Ignore unescaped whitespace and #-comments in the pattern.
Execute alias Flag.DFA_SHORTEST: In DFA mode, prefer the shortest possible match."""
    FIRSTLINE: int = 0x00000100
    """Compile: Restrict matches so they cannot cross newline boundaries in the subject.
Execute alias Flag.SUBSTITUTE_GLOBAL: Replace every occurrence in the subject during substitution."""
    MATCH_UNSET_BACKREF: int = 0x00000200
    """Compile: Treat unset backreferences as empty matches during execution.
Execute alias Flag.SUBSTITUTE_EXTENDED: Treat the replacement string using extended whitespace/comment syntax."""
    MULTILINE: int = 0x00000400
    """Compile: Allow ^ and $ to match immediately after and before internal newlines.
Execute alias Flag.SUBSTITUTE_UNSET_EMPTY: Treat unset captures in replacements as empty strings."""
    NEVER_UCP: int = 0x00000800
    """Compile: Forbid enabling Unicode property support for this pattern.
Execute alias Flag.SUBSTITUTE_UNKNOWN_UNSET: Treat unknown group names in replacements as unset captures."""
    NEVER_UTF: int = 0x00001000
    """Compile: Forbid enabling UTF mode via options or inline escapes.
Execute alias Flag.SUBSTITUTE_OVERFLOW_LENGTH: Report truncated output lengths when replacement buffers overflow."""
    NO_AUTO_CAPTURE: int = 0x00002000
    """Compile: Treat parentheses as non-capturing unless explicitly marked.
Execute alias Flag.NO_JIT_EXEC: Disable JIT execution for this match attempt."""
    NO_AUTO_POSSESS: int = 0x00004000
    """Compile: Disable automatic possessification optimizations.
Execute alias Flag.COPY_MATCHED_SUBJECT: Copy the matched subject substring into the match data block."""
    NO_DOTSTAR_ANCHOR: int = 0x00008000
    """Compile: Disable implicit anchoring for leading dot-star constructs.
Execute alias Flag.SUBSTITUTE_LITERAL: Treat the replacement string as literal text."""
    NO_START_OPTIMIZE: int = 0x00010000
    """Compile: Disable start-of-match optimizations such as fast-forwarding.
Execute alias Flag.SUBSTITUTE_MATCHED: Write only the matched substring to the output during substitution."""
    UCP: int = 0x00020000
    """Compile: Use Unicode character properties for character classification.
Execute alias Flag.SUBSTITUTE_REPLACEMENT_ONLY: Write only replacement text, omitting unmatched regions."""
    UNGREEDY: int = 0x00040000
    """Compile: Invert quantifier greediness so they are lazy by default.
Execute alias Flag.DISABLE_RECURSELOOP_CHECK: Disable recursion-loop detection during matching."""
    UTF: int = 0x00080000
    """Compile: Treat the pattern and subject as UTF strings."""
    NEVER_BACKSLASH_C: int = 0x00100000
    """Compile: Forbid use of the \\C escape."""
    ALT_CIRCUMFLEX: int = 0x00200000
    """Compile: Allow ^ in multiline mode to match after a trailing newline."""
    ALT_VERBNAMES: int = 0x00400000
    """Compile: Enable escape processing inside verb names such as (*MARK:NAME)."""
    USE_OFFSET_LIMIT: int = 0x00800000
    """Compile: Allow match contexts to enforce an explicit offset limit."""
    EXTENDED_MORE: int = 0x01000000
    """Compile: Apply extended-syntax whitespace and comment skipping to more constructs."""
    LITERAL: int = 0x02000000
    """Compile: Compile the pattern as literal text with metacharacters disabled."""
    MATCH_INVALID_UTF: int = 0x04000000
    """Compile: Allow matching subjects that contain invalid UTF sequences."""
    ALT_EXTENDED_CLASS: int = 0x08000000
    """Compile: Enable Unicode extended character class syntax inside []."""

    NOTBOL: 'Flag' = ALLOW_EMPTY_CLASS
    NOTEOL: 'Flag' = ALT_BSUX
    NOTEMPTY: 'Flag' = AUTO_CALLOUT
    NOTEMPTY_ATSTART: 'Flag' = CASELESS
    PARTIAL_SOFT: 'Flag' = DOLLAR_ENDONLY
    PARTIAL_HARD: 'Flag' = DOTALL
    DFA_RESTART: 'Flag' = DUPNAMES
    DFA_SHORTEST: 'Flag' = EXTENDED
    SUBSTITUTE_GLOBAL: 'Flag' = FIRSTLINE
    SUBSTITUTE_EXTENDED: 'Flag' = MATCH_UNSET_BACKREF
    SUBSTITUTE_UNSET_EMPTY: 'Flag' = MULTILINE
    SUBSTITUTE_UNKNOWN_UNSET: 'Flag' = NEVER_UCP
    SUBSTITUTE_OVERFLOW_LENGTH: 'Flag' = NEVER_UTF
    NO_JIT_EXEC: 'Flag' = NO_AUTO_CAPTURE
    COPY_MATCHED_SUBJECT: 'Flag' = NO_AUTO_POSSESS
    SUBSTITUTE_LITERAL: 'Flag' = NO_DOTSTAR_ANCHOR
    SUBSTITUTE_MATCHED: 'Flag' = NO_START_OPTIMIZE
    SUBSTITUTE_REPLACEMENT_ONLY: 'Flag' = UCP
    DISABLE_RECURSELOOP_CHECK: 'Flag' = UNGREEDY


PY_ONLY_FLAG_NAMES = ("NO_UTF", "NO_UCP", "JIT", "NO_JIT", "THREADS", "NO_THREADS", "COMPAT_UNICODE_ESCAPE")
PY_ONLY_FLAG_MASK: int = 0
PY_ONLY_FLAG_MASK |= int(Flag.NO_UTF)
PY_ONLY_FLAG_MASK |= int(Flag.NO_UCP)
PY_ONLY_FLAG_MASK |= int(Flag.JIT)
PY_ONLY_FLAG_MASK |= int(Flag.NO_JIT)
PY_ONLY_FLAG_MASK |= int(Flag.THREADS)
PY_ONLY_FLAG_MASK |= int(Flag.NO_THREADS)
PY_ONLY_FLAG_MASK |= int(Flag.COMPAT_UNICODE_ESCAPE)


def strip_py_only_flags(flags: int) -> int:
    """Remove Python-only option bits that the C engine does not understand."""
    return flags & ~PY_ONLY_FLAG_MASK

JIT = Flag.JIT
NO_JIT = Flag.NO_JIT
THREADS = Flag.THREADS
NO_THREADS = Flag.NO_THREADS
NO_UTF = Flag.NO_UTF
NO_UCP = Flag.NO_UCP
COMPAT_UNICODE_ESCAPE = Flag.COMPAT_UNICODE_ESCAPE

__all__ = [
    "Flag",
    "PY_ONLY_FLAG_NAMES",
    "PY_ONLY_FLAG_MASK",
    "strip_py_only_flags",
    "JIT",
    "NO_JIT",
    "THREADS",
    "NO_THREADS",
    "NO_UTF",
    "NO_UCP",
    "COMPAT_UNICODE_ESCAPE",
]
