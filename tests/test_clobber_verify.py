# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Verifying clobber: random VALID regexes checked for accuracy, leaks, crashes.

Unlike test_clobber/test_clobber_thread (which hammer the API with mostly
well-formed inputs and only require "no unexpected exception"), every
operation here is *verified*:

- accuracy: patterns are generated from a structured builder that stays inside
  the ``re``-compatible dialect, and every result (spans, group values, group
  spans, findall/split/subn output) is compared against ``re`` on the same
  inputs — including random pos/endpos and astral-plane subjects.  A second
  pool uses PCRE2-only syntax (\\p{..}, atomic groups, possessive quantifiers,
  \\R, \\Q..\\E, conditionals) and checks structural invariants instead.
- concurrency accuracy: a single shared finditer iterator is drained by all
  workers at once and the union of harvested matches must equal the
  single-threaded reference exactly (every match yielded exactly once).
- leaks: a steady-state phase asserts allocated-block and RSS growth stay
  bounded after warm-up.
- crashes/deadlocks: workers run under a join watchdog; a hang dumps stacks
  and fails instead of wedging the suite.

Workers use all cores minus one.  Duration is tunable via
``PYPCRE_CLOBBER_VERIFY_SECONDS`` and the seed via ``PYPCRE_CLOBBER_SEED``.
"""

from __future__ import annotations

import faulthandler
import gc
import os
import random
import re
import string
import sys
import threading
import time
import traceback
from dataclasses import dataclass, field

import pcre
import pytest


_WORKERS = max(1, (os.cpu_count() or 2) - 1)
_DIFF_DURATION = float(os.getenv("PYPCRE_CLOBBER_VERIFY_SECONDS", "45"))
_EXT_DURATION = max(10.0, _DIFF_DURATION * 0.6)
_JOIN_GRACE = 60.0
_MAX_DIFF_SUBJECT = 48  # short: the re oracle has no backtracking limits


def _pcre2_runtime_version() -> tuple[int, int]:
    import pcre_ext_c

    text = str(getattr(pcre_ext_c, "PCRE2_VERSION", "0.0")).split()[0]
    major, _, minor = text.partition(".")
    try:
        return int(major), int(minor)
    except ValueError:
        return (0, 0)


# Older PCRE2 runtimes have known engine-level wrong-result bugs that pypcre
# cannot paper over (e.g. 10.42 start-optimization loses matches for
# (?=2{1,3}\D?)(?:.?2){1,1}e{0,} in BOTH interpreter and JIT unless compiled
# with PCRE2_NO_START_OPTIMIZE; fixed by 10.46).  Accuracy mismatches on such
# runtimes are reported rather than failed; crashes, errors and hangs still
# fail everywhere.  PYPCRE_CLOBBER_STRICT_ENGINE=1 restores hard failures.
_TRUSTED_ENGINE = (
    _pcre2_runtime_version() >= (10, 46)
    or bool(os.getenv("PYPCRE_CLOBBER_STRICT_ENGINE"))
)
_MAX_EXT_SUBJECT = 2048


def _system_seed() -> int:
    configured = os.getenv("PYPCRE_CLOBBER_SEED")
    if configured is not None:
        return int(configured, 0)
    return int.from_bytes(os.urandom(8), "little")


def _assert_expected_gil_state() -> None:
    """On a free-threaded build the extension must keep the GIL disabled."""

    import sysconfig

    if sysconfig.get_config_var("Py_GIL_DISABLED"):
        assert not sys._is_gil_enabled(), (
            "free-threaded build re-enabled the GIL: the extension no longer "
            "declares Py_MOD_GIL_NOT_USED correctly"
        )


# ---------------------------------------------------------------------------
# Random-but-valid pattern generation
# ---------------------------------------------------------------------------

# Alphabet shared by pattern literals and subjects so matches are dense.
_LITERAL_CHARS = "abcde012 _-"
_UNICODE_EXTRAS = "éÉ€\U00010348\U0001f600"  # é É € 𐍈 😀

_CLASS_ITEMS = ("a-f", "g-k", "0-5", "x", "_", " ", r"\d", r"\w", r"\s")


@dataclass
class _GenCtx:
    rng: random.Random
    allow_pcre_only: bool
    is_bytes: bool
    # Regex group numbers follow OPENING-parenthesis order, and backrefs /
    # conditionals may only reference groups that are fully CLOSED at the
    # reference point — a reference into a still-open enclosing group is a
    # semantic dark corner where engines legitimately diverge.
    next_group_no: int = 1
    closed_nums: list[int] = field(default_factory=list)
    names: list[str] = field(default_factory=list)
    depth: int = 0
    # Polynomial-blowup cap: at most this many unbounded quantifiers per
    # pattern (each adjacent unbounded atom adds a backtracking degree, and
    # the re oracle has no match limit).
    unbounded_budget: int = 2


def _gen_literal(ctx: _GenCtx) -> str:
    ch = ctx.rng.choice(_LITERAL_CHARS)
    if not ctx.is_bytes and ctx.rng.random() < 0.08:
        ch = ctx.rng.choice(_UNICODE_EXTRAS)
    return re.escape(ch)


def _gen_class(ctx: _GenCtx) -> str:
    items = ctx.rng.sample(_CLASS_ITEMS, ctx.rng.randint(1, 3))
    neg = "^" if ctx.rng.random() < 0.25 else ""
    return "[" + neg + "".join(items) + "]"


def _gen_atom(ctx: _GenCtx) -> str:
    r = ctx.rng.random()
    if r < 0.40:
        return _gen_literal(ctx)
    if r < 0.60:
        return _gen_class(ctx)
    if r < 0.70:
        return ctx.rng.choice((r"\d", r"\w", r"\s", r"\D", r"\W", r"\S"))
    if r < 0.76:
        return "."
    if ctx.allow_pcre_only and r < 0.80:
        return ctx.rng.choice((r"\p{L}", r"\p{N}", r"\p{Lu}", r"\R", r"\Qa.b\E"))
    return _gen_literal(ctx)


def _bounded_range(ctx: _GenCtx, max_min: int, max_extra: int) -> str:
    m = ctx.rng.randint(0, max_min)
    n = m + ctx.rng.randint(0, max_extra)
    if n == 0:
        # {0,0} stays out of the differential pool: PCRE2 10.46 fails to
        # match ((z)x|(?=w)y){0,0} (which reduces to the empty pattern)
        # while re matches empty at every position.
        n = 1
    return f"{{{m},{n}}}"


def _quantifier(ctx: _GenCtx, bounded_only: bool) -> str:
    r = ctx.rng.random()
    if bounded_only or (r < 0.75 and ctx.unbounded_budget <= 0):
        quant = "?" if r < 0.4 else _bounded_range(ctx, 2, 2)
    else:
        if r < 0.25:
            quant = "?"
        elif r < 0.5:
            quant = "*"
            ctx.unbounded_budget -= 1
        elif r < 0.75:
            quant = "+"
            ctx.unbounded_budget -= 1
        elif r < 0.9:
            quant = _bounded_range(ctx, 3, 3)
        else:
            quant = f"{{{ctx.rng.randint(0, 2)},}}"
            ctx.unbounded_budget -= 1
    mode = ctx.rng.random()
    if mode < 0.2:
        quant += "?"  # lazy
    elif mode < 0.3 and ctx.allow_pcre_only:
        # Possessive quantifiers stay out of the differential pool: PCRE2's
        # auto-possessification optimization can change match results around
        # explicit possessives (observed on 10.46: (\S*?|.?2*)(b{2,2}|(c??
        # [a-fg-k ]+|[\d\w]??_)){1,1}(\-{1,2})?+a fails on b'd_S2ea1-' unless
        # compiled with PCRE2_NO_AUTO_POSSESS, while re matches (0, 6)).
        quant += "+"
    return quant


def _quantifier_min(quant: str) -> int:
    if quant.startswith("{"):
        digits = quant[1:].split(",", 1)[0].rstrip("}")
        return int(digits)
    return 1 if quant.startswith("+") else 0


def _gen_piece(ctx: _GenCtx, bounded_only: bool) -> tuple[str, bool]:
    """One quantifiable unit; returns (text, nullable).

    Two guards, both required because the re oracle has neither backtracking
    limits nor PCRE2's empty-iteration rules:
    - catastrophic backtracking: unbounded quantifiers only attach to single
      atoms (linear for any backtracker); groups only receive bounded
      quantifiers, and everything inside a quantified group is itself bounded.
    - empty-repeat divergence: a quantified group whose body can match the
      empty string is resolved differently by re and PCRE2 (iteration counting
      for {m,n} over nullable bodies), so nullability is tracked and nullable
      group bodies are never quantified.
    """

    r = ctx.rng.random()
    if ctx.depth < 3 and r < 0.22:
        return _gen_group(ctx, bounded_only)
    if r < 0.30 and ctx.depth < 3:
        return _gen_lookaround(ctx), True
    if r < 0.34 and ctx.closed_nums:
        if ctx.names and ctx.rng.random() < 0.4:
            return f"(?P={ctx.rng.choice(ctx.names)})", True
        return f"\\{ctx.rng.choice(ctx.closed_nums)}", True
    atom = _gen_atom(ctx)
    if ctx.rng.random() < 0.55:
        quant = _quantifier(ctx, bounded_only=bounded_only)
        return atom + quant, _quantifier_min(quant) == 0
    return atom, False


def _gen_seq(ctx: _GenCtx, bounded_only: bool, max_pieces: int = 4) -> tuple[str, bool]:
    ctx.depth += 1
    try:
        pieces = [
            _gen_piece(ctx, bounded_only)
            for _ in range(ctx.rng.randint(1, max_pieces))
        ]
        seq = "".join(text for text, _null in pieces)
        nullable = all(null for _text, null in pieces)
        if ctx.rng.random() < 0.25:
            alt_pieces = [
                _gen_piece(ctx, bounded_only)
                for _ in range(ctx.rng.randint(1, 2))
            ]
            seq = seq + "|" + "".join(text for text, _null in alt_pieces)
            nullable = nullable or all(null for _text, null in alt_pieces)
        return seq, nullable
    finally:
        ctx.depth -= 1


def _gen_group(ctx: _GenCtx, parent_bounded: bool) -> tuple[str, bool]:
    r = ctx.rng.random()
    quantify = ctx.rng.random() < 0.5
    if r < 0.42 and ctx.closed_nums and not (r < 0.36 and ctx.allow_pcre_only) and r >= 0.30:
        # Conditional on an already-closed group (see _GenCtx numbering note).
        cond = ctx.rng.choice(ctx.closed_nums)
        yes = _gen_atom(ctx)
        no = _gen_atom(ctx)
        return f"(?({cond}){yes}|{no})", False
    capturing = r >= 0.42
    number = 0
    if capturing:
        # Claim the number at OPEN time: regex numbering is by open paren.
        number = ctx.next_group_no
        ctx.next_group_no += 1
    # Anything inside a quantified group must be bounded (see _gen_piece) —
    # and boundedness PROPAGATES: a group nested anywhere under a quantifier
    # must be fully bounded, or (?:.+X){2,4}-style blow-ups sneak through.
    body, body_nullable = _gen_seq(
        ctx, bounded_only=parent_bounded or quantify, max_pieces=3
    )
    # Never quantify a group whose body can match empty (see _gen_piece), and
    # never quantify a group nested inside an already-quantified group: even
    # with bounded ranges, stacked group quantifiers multiply the re oracle's
    # backtracking (e.g. ((.{1,3}|0{2,4}){1,3}\1?){2,4} took 30 s per call).
    quantify = quantify and not body_nullable and not parent_bounded
    if not capturing:
        if r < 0.30 or not ctx.allow_pcre_only:
            out = f"(?:{body})"
        else:
            out = f"(?>{body})"  # atomic group: PCRE-only pool
    elif r < 0.60:
        name = f"g{number}_{ctx.rng.randint(0, 999)}"
        out = f"(?P<{name}>{body})"
        ctx.closed_nums.append(number)
        ctx.names.append(name)
    else:
        out = f"({body})"
        ctx.closed_nums.append(number)
    if quantify:
        quant = _quantifier(ctx, bounded_only=True)
        return out + quant, _quantifier_min(quant) == 0
    return out, body_nullable


def _gen_lookaround(ctx: _GenCtx) -> str:
    kind = ctx.rng.random()
    if kind < 0.5:
        opener = "(?=" if kind < 0.25 else "(?!"
        body, _nullable = _gen_seq(ctx, bounded_only=True, max_pieces=2)
    else:
        opener = "(?<=" if kind < 0.75 else "(?<!"
        # Lookbehind must be fixed width for both engines: literals/classes
        # only, no quantifiers, single branch.
        parts = []
        for _ in range(ctx.rng.randint(1, 2)):
            parts.append(
                _gen_class(ctx) if ctx.rng.random() < 0.4 else _gen_literal(ctx)
            )
        body = "".join(parts)
    return opener + body + ")"


def _gen_pattern(rng: random.Random, *, allow_pcre_only: bool, is_bytes: bool) -> str:
    ctx = _GenCtx(rng=rng, allow_pcre_only=allow_pcre_only, is_bytes=is_bytes)
    pattern, _nullable = _gen_seq(ctx, bounded_only=False, max_pieces=5)
    if rng.random() < 0.20:
        pattern = rng.choice(("(?i)", "(?m)", "(?s)", "(?im)", "(?s)(?i)")) + pattern
    if rng.random() < 0.12:
        pattern = rng.choice(("^", r"\A", r"\b")) + pattern
    if rng.random() < 0.12:
        pattern = pattern + rng.choice(("$", r"\b"))
    return pattern


def _gen_subject(rng: random.Random, pattern: str, *, is_bytes: bool, limit: int) -> str | bytes:
    # Bias toward pattern literals so matches are dense.
    literal_pool = [c for c in pattern if c.isalnum() or c in " _-"] or list("abc")
    alphabet = _LITERAL_CHARS + "\n\t"
    out: list[str] = []
    length = rng.randint(0, limit)
    while len(out) < length:
        r = rng.random()
        if r < 0.5:
            out.append(rng.choice(literal_pool))
        elif r < 0.9 or is_bytes:
            out.append(rng.choice(alphabet))
        else:
            out.append(rng.choice(_UNICODE_EXTRAS))
    subject = "".join(out)
    if is_bytes:
        return subject.encode("ascii", "ignore")
    return subject


# ---------------------------------------------------------------------------
# Verification helpers
# ---------------------------------------------------------------------------


def _match_signature(m) -> tuple | None:
    if m is None:
        return None
    n = len(m.groups())
    return (
        m.span(),
        m.groups(),
        tuple(m.span(i) for i in range(n + 1)),
        tuple(sorted(m.groupdict().items())),
    )


def _check_invariants(m, subject, *, where: str) -> None:
    if m is None:
        return
    length = len(subject)
    n = len(m.groups())
    for i in range(n + 1):
        start, end = m.span(i)
        if start == -1 or end == -1:
            assert (start, end) == (-1, -1), f"{where}: half-unset span {i}"
            assert m.group(i) is None, f"{where}: unset group {i} has value"
            continue
        assert 0 <= start <= end <= length, (
            f"{where}: span {i} out of bounds: {(start, end)} len={length}"
        )
        assert m.group(i) == subject[start:end], (
            f"{where}: group {i} != subject slice"
        )


def _drain_spans(compiled, subject) -> list[tuple[int, int]]:
    return [m.span() for m in compiled.finditer(subject)]


# Each worker publishes its in-flight case here so a hang identifies the
# pattern/subject that wedged an engine instead of just timing out.
_CURRENT_CASE: dict[int, str] = {}


class _Failures:
    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._items: list[str] = []
        self.stop = threading.Event()

    def record(self, message: str, *, halt: bool = True) -> None:
        with self._lock:
            if len(self._items) < 10:
                self._items.append(message)
        if halt:
            self.stop.set()

    def items(self) -> list[str]:
        with self._lock:
            return list(self._items)


def _run_workers(target, duration: float, failures: _Failures, seed: int, ops: list[int]) -> None:
    deadline = time.monotonic() + duration
    threads = []
    for worker_id in range(_WORKERS):
        t = threading.Thread(
            target=target,
            args=(worker_id, seed + worker_id * 7919, deadline, failures, ops),
            name=f"clobber-verify-{worker_id}",
            daemon=True,
        )
        threads.append(t)
        t.start()
    # One shared grace budget for ALL joins: per-thread timeouts would let N
    # wedged workers stack up N grace periods.
    join_deadline = deadline + _JOIN_GRACE
    for t in threads:
        t.join(max(0.0, join_deadline - time.monotonic()))
    hung = [t for t in threads if t.is_alive()]
    if hung:
        failures.stop.set()
        faulthandler.dump_traceback()
        wedged = [
            f"{t.name}: {_CURRENT_CASE.get(int(t.name.rsplit('-', 1)[1]), '<unknown case>')}"
            for t in hung
        ]
        pytest.fail(
            "hung workers (engine wedged on case?):\n" + "\n".join(wedged)
        )


# ---------------------------------------------------------------------------
# Test 1: differential accuracy against re, all cores - 1
# ---------------------------------------------------------------------------


def _differential_case(
    rng: random.Random,
    failures: _Failures,
    seed: int,
    jit_divergences: "_Failures",
    worker_id: int,
) -> None:
    is_bytes = rng.random() < 0.30
    pattern = _gen_pattern(rng, allow_pcre_only=False, is_bytes=is_bytes)
    pattern_input: str | bytes = pattern.encode("ascii") if is_bytes else pattern
    _CURRENT_CASE[worker_id] = f"pattern={pattern_input!r}"

    try:
        oracle = re.compile(pattern_input)
    except re.error:
        return  # generator slipped outside re's dialect; not a pypcre concern
    try:
        # Strict comparisons run on the interpreter: PCRE2's JIT engine has
        # known wrong-result bugs (e.g. 10.46 sljit misses the match for
        # d?1([a-f]{0,2}(?=[^_]))- on b'1-0') that pypcre cannot fix.  JIT
        # parity is checked separately below and reported without failing
        # unless PYPCRE_CLOBBER_STRICT_JIT is set.
        compiled = pcre.compile(pattern_input, flags=pcre.Flag.NO_JIT)
    except Exception as exc:
        failures.record(
            f"pcre rejects re-valid pattern: seed={seed} pattern={pattern_input!r} error={exc!r}"
        )
        return
    jit_compiled = None
    if rng.random() < 0.5:
        try:
            jit_compiled = pcre.compile(pattern_input)
        except Exception as exc:
            failures.record(
                f"jit compile diverges: seed={seed} pattern={pattern_input!r} error={exc!r}"
            )
            return

    for _ in range(rng.randint(1, 3)):
        subject = _gen_subject(rng, pattern, is_bytes=is_bytes, limit=_MAX_DIFF_SUBJECT)
        length = len(subject)
        pos = rng.randint(0, length) if rng.random() < 0.35 else 0
        endpos = rng.randint(pos, length) if rng.random() < 0.25 else length
        _CURRENT_CASE[worker_id] = (
            f"pattern={pattern_input!r} subject={subject!r} pos={pos} endpos={endpos}"
        )

        def fail(op: str, got, want) -> None:
            message = (
                f"MISMATCH {op}: seed={seed} pattern={pattern_input!r} "
                f"subject={subject!r} pos={pos} endpos={endpos} "
                f"pcre={got!r} re={want!r}"
            )
            if _TRUSTED_ENGINE:
                failures.record(message)
            else:
                _ENGINE_DIVERGENCES.record(message, halt=False)

        try:
            for op in ("search", "match", "fullmatch"):
                got_m = getattr(compiled, op)(subject, pos, endpos)
                want_m = getattr(oracle, op)(subject, pos, endpos)
                _check_invariants(got_m, subject, where=f"{op} seed={seed}")
                got, want = _match_signature(got_m), _match_signature(want_m)
                if got != want:
                    fail(op, got, want)
                    return

            got_all = compiled.findall(subject)
            want_all = oracle.findall(subject)
            if got_all != want_all:
                fail("findall", got_all, want_all)
                return

            got_iter = [_match_signature(m) for m in compiled.finditer(subject)]
            want_iter = [_match_signature(m) for m in oracle.finditer(subject)]
            if got_iter != want_iter:
                fail("finditer", got_iter, want_iter)
                return

            maxsplit = rng.randint(0, 3)
            got_split = compiled.split(subject, maxsplit=maxsplit)
            want_split = oracle.split(subject, maxsplit=maxsplit)
            if got_split != want_split:
                fail(f"split(maxsplit={maxsplit})", got_split, want_split)
                return

            template: str | bytes = b"<X>" if is_bytes else "<X>"
            if oracle.groups >= 1 and rng.random() < 0.5:
                template = b"[\\1]" if is_bytes else "[\\1]"
            count = rng.randint(0, 2)
            got_subn = compiled.subn(template, subject, count=count)
            want_subn = oracle.subn(template, subject, count=count)
            if got_subn != want_subn:
                fail(f"subn(count={count})", got_subn, want_subn)
                return

            if jit_compiled is not None:
                jit_iter = [_match_signature(m) for m in jit_compiled.finditer(subject)]
                if jit_iter != want_iter:
                    jit_divergences.record(
                        f"JIT-vs-interpreter divergence (PCRE2 engine): seed={seed} "
                        f"pattern={pattern_input!r} subject={subject!r} "
                        f"jit={jit_iter!r} interp/re={want_iter!r}",
                        halt=False,
                    )
        except pcre.error as exc:
            # Engine resource limits are a legal outcome on an unlucky
            # pattern/subject combination, not an accuracy failure.
            if "limit" in str(exc).lower():
                return
            tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            failures.record(
                f"operation error: seed={seed} pattern={pattern_input!r} "
                f"subject={subject!r} pos={pos} endpos={endpos} error={exc!r}\n{tb}"
            )
            return
        except Exception as exc:
            tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            failures.record(
                f"operation error: seed={seed} pattern={pattern_input!r} "
                f"subject={subject!r} pos={pos} endpos={endpos} error={exc!r}\n{tb}"
            )
            return


_JIT_DIVERGENCES = _Failures()
_ENGINE_DIVERGENCES = _Failures()


def _differential_worker(worker_id: int, seed: int, deadline: float, failures: _Failures, ops: list[int]) -> None:
    rng = random.Random(seed)
    try:
        while time.monotonic() < deadline and not failures.stop.is_set():
            _differential_case(rng, failures, seed, _JIT_DIVERGENCES, worker_id)
            ops[worker_id] += 1
    except Exception as exc:  # pragma: no cover - surfaced via failures
        tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        failures.record(f"worker crash: worker={worker_id} seed={seed} error={exc!r}\n{tb}")


def test_clobber_differential_accuracy_threaded() -> None:
    _assert_expected_gil_state()
    seed = _system_seed()
    print(f"[clobber-verify diff] seed={seed} workers={_WORKERS}", flush=True)
    failures = _Failures()
    ops = [0] * _WORKERS
    _run_workers(_differential_worker, _DIFF_DURATION, failures, seed, ops)
    if failures.items():
        pytest.fail("\n---\n".join(failures.items()))
    engine_reports = _ENGINE_DIVERGENCES.items()
    if engine_reports:
        print(
            f"[clobber-verify diff] {len(engine_reports)} accuracy divergence(s) "
            f"on untrusted PCRE2 runtime {_pcre2_runtime_version()} (reported only):",
            flush=True,
        )
        for report in engine_reports[:3]:
            print("  " + report, flush=True)
    jit_reports = _JIT_DIVERGENCES.items()
    if jit_reports:
        print(
            f"[clobber-verify diff] {len(jit_reports)} PCRE2 JIT-engine "
            "divergence(s) observed (upstream engine, not pypcre):",
            flush=True,
        )
        for report in jit_reports[:3]:
            print("  " + report, flush=True)
        if os.getenv("PYPCRE_CLOBBER_STRICT_JIT"):
            pytest.fail("\n---\n".join(jit_reports))
    total = sum(ops)
    print(f"[clobber-verify diff] cases={total}", flush=True)
    assert total > 0


# ---------------------------------------------------------------------------
# Test 2: PCRE2-extended syntax, invariants + cross-op consistency
# ---------------------------------------------------------------------------


def _extended_case(rng: random.Random, failures: _Failures, seed: int, worker_id: int) -> None:
    is_bytes = rng.random() < 0.30
    pattern = _gen_pattern(rng, allow_pcre_only=not is_bytes, is_bytes=is_bytes)
    pattern_input: str | bytes = pattern.encode("ascii", "ignore") if is_bytes else pattern
    _CURRENT_CASE[worker_id] = f"ext pattern={pattern_input!r}"

    try:
        compiled = pcre.compile(pattern_input)
    except pcre.error:
        return  # extended pool may produce PCRE2-invalid combinations
    except Exception as exc:
        failures.record(
            f"unexpected compile error: seed={seed} pattern={pattern_input!r} error={exc!r}"
        )
        return

    subject = _gen_subject(rng, pattern, is_bytes=is_bytes, limit=_MAX_EXT_SUBJECT)
    try:
        matches = list(compiled.finditer(subject))
        spans = []
        prev_end = -1
        for m in matches:
            _check_invariants(m, subject, where=f"ext finditer seed={seed}")
            start, end = m.span()
            assert start >= prev_end or (start == prev_end == end), (
                f"overlapping/descending finditer spans: seed={seed} "
                f"pattern={pattern_input!r} spans={spans + [(start, end)]}"
            )
            prev_end = end
            spans.append((start, end))

        # Cross-operation consistency: every op must observe the same matches.
        n_findall = len(compiled.findall(subject))
        assert n_findall == len(matches), (
            f"findall/finditer count mismatch: seed={seed} pattern={pattern_input!r} "
            f"findall={n_findall} finditer={len(matches)}"
        )
        repl: str | bytes = b"" if is_bytes else ""
        n_subn = compiled.subn(repl, subject)[1]
        assert n_subn == len(matches), (
            f"subn/finditer count mismatch: seed={seed} pattern={pattern_input!r} "
            f"subn={n_subn} finditer={len(matches)}"
        )

        m = compiled.search(subject)
        _check_invariants(m, subject, where=f"ext search seed={seed}")
        if spans:
            assert m is not None and m.span() == spans[0], (
                f"search disagrees with first finditer match: seed={seed} "
                f"pattern={pattern_input!r} search={m and m.span()} first={spans[0]}"
            )
        else:
            assert m is None, (
                f"search found a match finditer missed: seed={seed} "
                f"pattern={pattern_input!r} span={m.span()}"
            )
    except AssertionError as exc:
        failures.record(str(exc))
    except pcre.error:
        return  # runtime limits (match/depth) are acceptable on wild patterns
    except Exception as exc:
        tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        failures.record(
            f"ext operation error: seed={seed} pattern={pattern_input!r} error={exc!r}\n{tb}"
        )


def _extended_worker(worker_id: int, seed: int, deadline: float, failures: _Failures, ops: list[int]) -> None:
    rng = random.Random(seed)
    try:
        while time.monotonic() < deadline and not failures.stop.is_set():
            _extended_case(rng, failures, seed, worker_id)
            ops[worker_id] += 1
    except Exception as exc:  # pragma: no cover
        tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        failures.record(f"worker crash: worker={worker_id} seed={seed} error={exc!r}\n{tb}")


def test_clobber_extended_invariants_threaded() -> None:
    _assert_expected_gil_state()
    seed = _system_seed() ^ 0xE7E7E7E7
    print(f"[clobber-verify ext] seed={seed} workers={_WORKERS}", flush=True)
    failures = _Failures()
    ops = [0] * _WORKERS
    _run_workers(_extended_worker, _EXT_DURATION, failures, seed, ops)
    if failures.items():
        pytest.fail("\n---\n".join(failures.items()))
    total = sum(ops)
    print(f"[clobber-verify ext] cases={total}", flush=True)
    assert total > 0


# ---------------------------------------------------------------------------
# Test 3: shared-iterator concurrency accuracy (every match exactly once)
# ---------------------------------------------------------------------------


def test_clobber_shared_finditer_exactly_once() -> None:
    _assert_expected_gil_state()
    seed = _system_seed() ^ 0x51713D
    rng = random.Random(seed)
    print(f"[clobber-verify iter] seed={seed} workers={_WORKERS}", flush=True)

    for round_no in range(6):
        is_bytes = round_no % 3 == 2
        # Dense matches over a large subject (large enough to cross the
        # GIL-release threshold so iternext runs concurrently on GIL builds).
        word = b"w%d " % round_no if is_bytes else f"w{round_no}Δ "
        reps = 60_000
        subject = word * reps
        pattern_input: str | bytes = rb"\w+" if is_bytes else r"\w+"
        compiled = pcre.compile(pattern_input)

        expected = [m.span() for m in compiled.finditer(subject)]
        assert len(expected) == reps

        shared_iter = compiled.finditer(subject)
        buckets: list[list[tuple]] = [[] for _ in range(_WORKERS)]
        errors: list[str] = []
        start_gate = threading.Barrier(_WORKERS)

        def drain(idx: int) -> None:
            try:
                start_gate.wait(timeout=30)
                bucket = buckets[idx]
                for m in shared_iter:
                    bucket.append((m.span(), m.group()))
            except Exception as exc:  # pragma: no cover
                errors.append(f"round={round_no} worker={idx} error={exc!r}")

        threads = [
            threading.Thread(target=drain, args=(i,), daemon=True)
            for i in range(_WORKERS)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=_JOIN_GRACE)
        hung = [t for t in threads if t.is_alive()]
        if hung:
            faulthandler.dump_traceback()
            pytest.fail(f"shared finditer deadlock: round={round_no} seed={seed}")
        assert not errors, errors

        harvested = [item for bucket in buckets for item in bucket]
        assert len(harvested) == len(expected), (
            f"shared finditer lost/duplicated matches: round={round_no} seed={seed} "
            f"harvested={len(harvested)} expected={len(expected)}"
        )
        harvested_spans = sorted(span for span, _group in harvested)
        assert harvested_spans == expected, (
            f"shared finditer span set diverged: round={round_no} seed={seed}"
        )
        for span, group in harvested:
            assert group == subject[span[0]:span[1]], (
                f"shared finditer group/slice mismatch: round={round_no} seed={seed} span={span}"
            )
        _ = rng  # reserved for future randomized rounds


# ---------------------------------------------------------------------------
# Test 4: leak stability (steady-state allocations and RSS)
# ---------------------------------------------------------------------------


def _rss_bytes() -> int:
    try:
        with open("/proc/self/status", "r", encoding="ascii") as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass
    return 0


def _leak_round(cases) -> None:
    for compiled, subject, template in cases:
        compiled.search(subject)
        compiled.match(subject)
        compiled.fullmatch(subject)
        for m in compiled.finditer(subject):
            m.group()
            m.groups()
            m.span()
        compiled.findall(subject)
        compiled.split(subject, maxsplit=2)
        compiled.subn(template, subject)
        m = compiled.search(subject)
        if m is not None:
            m.groupdict()
            try:
                m.expand(template)
            except (pcre.error, re.error, IndexError):
                pass


def test_clobber_leak_stability() -> None:
    _assert_expected_gil_state()
    seed = _system_seed() ^ 0x1EAC
    rng = random.Random(seed)
    print(f"[clobber-verify leak] seed={seed}", flush=True)

    cases = []
    while len(cases) < 40:
        is_bytes = len(cases) % 3 == 1
        pattern = _gen_pattern(rng, allow_pcre_only=not is_bytes, is_bytes=is_bytes)
        pattern_input: str | bytes = (
            pattern.encode("ascii", "ignore") if is_bytes else pattern
        )
        try:
            compiled = pcre.compile(pattern_input)
        except pcre.error:
            continue
        subject = _gen_subject(rng, pattern, is_bytes=is_bytes, limit=512)
        template: str | bytes = b"X" if is_bytes else "X"
        cases.append((compiled, subject, template))

    warmup_rounds, measured_rounds = 5, 40
    for _ in range(warmup_rounds):
        _leak_round(cases)
    gc.collect()
    gc.collect()
    baseline_blocks = sys.getallocatedblocks()
    baseline_rss = _rss_bytes()

    for _ in range(measured_rounds):
        _leak_round(cases)
    gc.collect()
    gc.collect()
    grown_blocks = sys.getallocatedblocks() - baseline_blocks
    grown_rss = _rss_bytes() - baseline_rss

    print(
        f"[clobber-verify leak] block growth={grown_blocks} rss growth={grown_rss}",
        flush=True,
    )
    # A per-operation leak would grow linearly with rounds (40 rounds x 40
    # cases x ~10 ops); steady-state noise stays far below these bounds.
    if baseline_blocks > 0:
        assert grown_blocks < 5_000, (
            f"allocated blocks grew by {grown_blocks} over {measured_rounds} "
            f"steady-state rounds (seed={seed})"
        )
    assert grown_rss < 48 * 1024 * 1024, (
        f"RSS grew by {grown_rss} bytes over {measured_rounds} steady-state "
        f"rounds (seed={seed})"
    )
