"""Reproducible API hot-path benchmark for CPython 3.10 and 3.14t.

Run from the repository root with either interpreter, for example::

    PYTHONPATH=. python3 benchmarks/api_hotpaths.py
    PYTHONPATH=. python3.14t benchmarks/api_hotpaths.py

The benchmark intentionally uses short subjects so Python dispatch, template
parsing, and object-wrapper costs are visible instead of being hidden by a
large PCRE2 scan.  Set ``PYPCRE_BENCH_RUNS`` and ``PYPCRE_BENCH_REPEATS`` to
change the sample size.
When running on a free-threaded build, an additional shared-pattern workload
checks the concurrent execution path.
"""

from __future__ import annotations

import concurrent.futures
import os
import re
import statistics
import sys
import time
import timeit
from collections.abc import Callable

import pcre

RUNS = int(os.getenv("PYPCRE_BENCH_RUNS", "10000"))
REPEATS = int(os.getenv("PYPCRE_BENCH_REPEATS", "5"))


def _time(fn: Callable[[], object]) -> float:
    # A separate Timer gives every operation a monomorphic call site. Keep the
    # default whole-suite duration short as well: on asymmetric macOS hosts a
    # sustained microbenchmark can migrate to efficiency cores despite its
    # performance-tier task policy, creating a visible step in later rows.
    samples = timeit.Timer(fn).repeat(repeat=REPEATS, number=RUNS)
    return statistics.median(samples) * 1_000_000.0 / RUNS


def main() -> int:
    subject = "x" * 1000
    short_subject = "x" * 10
    pattern = pcre.compile("(x)")
    named_pattern = pcre.compile("(?P<word>x)")
    captured = pattern.match(short_subject)
    named_captured = pcre.compile("(?P<word>x)").match(short_subject)
    multi_captured = pcre.compile("(?P<a>x)(?P<b>x)").match(short_subject)
    stdlib_flags = re.I | re.M | re.S | re.X
    if captured is None:
        raise AssertionError("benchmark pattern failed to produce a match")
    if named_captured is None:
        raise AssertionError("benchmark named pattern failed to produce a match")
    if multi_captured is None:
        raise AssertionError("benchmark multi pattern failed to produce a match")
    operations: list[tuple[str, Callable[[], object]]] = [
        ("bound.match", lambda: pattern.match(subject)),
        ("bound.search", lambda: pattern.search(subject)),
        ("bound.fullmatch", lambda: pattern.fullmatch(subject)),
        ("bound.findall", lambda: pattern.findall(short_subject)),
        ("bound.finditer", lambda: list(pattern.finditer(short_subject))),
        ("bound.split", lambda: pattern.split("x " * 8)),
        ("bound.sub.literal", lambda: pattern.sub("[X]", short_subject)),
        ("bound.sub.literal1", lambda: pattern.sub("[X]", short_subject, count=1)),
        ("bound.sub.literal4", lambda: pattern.sub("[X]", short_subject, count=4)),
        ("bound.sub.backref", lambda: pattern.sub(r"[\1]", short_subject)),
        ("bound.sub.backref1", lambda: pattern.sub(r"[\1]", short_subject, count=1)),
        ("bound.sub.backref4", lambda: pattern.sub(r"[\1]", short_subject, count=4)),
        ("bound.sub.explicit", lambda: pattern.sub(r"[\g<1>]", short_subject)),
        (
            "bound.sub.named",
            lambda: named_pattern.sub(r"[\g<word>]", short_subject),
        ),
        ("match.groups", captured.groups),
        ("match.first_lastindex", lambda: pattern.match("x").lastindex),
        ("module.escape.text", lambda: pcre.escape("identifier_123")),
        ("module.escape.bytes", lambda: pcre.escape(b"identifier123")),
        ("module.escape.special", lambda: pcre.escape("a+b [c]")),
        ("module.compile.reflags", lambda: pcre.compile("(x)", stdlib_flags)),
        ("match.expand.numeric", lambda: captured.expand(r"[\1]")),
        ("match.expand.explicit", lambda: captured.expand(r"[\g<1>]")),
        ("match.expand.named", lambda: named_captured.expand(r"[\g<word>]")),
        ("match.expand.escaped", lambda: named_captured.expand(r"\\\g<word>")),
        (
            "match.expand.multi",
            lambda: multi_captured.expand(r"[\g<a>]-\g<b>"),
        ),
        (
            "match.expand.three",
            lambda: multi_captured.expand(r"[\g<a>]-\g<b>-\g<a>"),
        ),
        ("module.match", lambda: pcre.match("(x)", subject)),
        ("module.search", lambda: pcre.search("(x)", subject)),
        ("module.fullmatch", lambda: pcre.fullmatch("(x)", subject)),
        ("module.findall", lambda: pcre.findall("(x)", short_subject)),
        ("module.finditer", lambda: list(pcre.finditer("(x)", short_subject))),
        ("module.split", lambda: pcre.split("(x)", "x " * 8)),
        ("module.sub.literal", lambda: pcre.sub("(x)", "[X]", short_subject)),
    ]

    gil_enabled = getattr(sys, "_is_gil_enabled", lambda: True)()
    print(
        f"runtime={sys.version.split()[0]} gil_enabled={gil_enabled} "
        f"runs={RUNS} repeats={REPEATS}"
    )
    for name, operation in operations:
        print(f"{name:22s} {_time(operation):8.3f} us")

    if not gil_enabled:
        workers = 8
        per_worker = max(1, RUNS // 5)

        def shared_search(_: int) -> int:
            for _ in range(per_worker):
                pattern.search(subject)
            return per_worker

        started = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            completed = sum(pool.map(shared_search, range(workers)))
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        print(f"shared.search.{workers}T {elapsed_ms:8.3f} ms ({completed} calls)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
