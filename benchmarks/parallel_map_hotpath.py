"""Measure the batched :func:`pcre.parallel_map` workload.

Run from the repository root with either interpreter::

    PYTHONPATH=. python3 benchmarks/parallel_map_hotpath.py
    PYTHONPATH=. python3.14t benchmarks/parallel_map_hotpath.py

The subjects are intentionally large enough for PCRE2 to amortize worker
startup and queueing.  This reports the serial baseline, threaded execution,
and the speedup while preserving result order.
"""

from __future__ import annotations

import os
import time
from collections.abc import Callable

import pcre


SUBJECT_COUNT = int(os.getenv("PYPCRE_PARALLEL_SUBJECTS", "16"))
SUBJECT_SIZE = int(os.getenv("PYPCRE_PARALLEL_SIZE", "1000000"))
RUNS = int(os.getenv("PYPCRE_PARALLEL_RUNS", "5"))


def _best_ms(fn: Callable[[], object]) -> float:
    callable_fn = fn  # Keep the timed call outside the loop's attribute lookup.
    best = float("inf")
    for _ in range(RUNS):
        started = time.perf_counter()
        callable_fn()
        best = min(best, (time.perf_counter() - started) * 1000.0)
    return best


def main() -> int:
    pattern = pcre.compile(r"\d+", pcre.Flag.THREADS)
    subjects = ["x" * SUBJECT_SIZE + "123"] * SUBJECT_COUNT
    serial = lambda: [pattern.search(subject) for subject in subjects]
    parallel = lambda: pattern.parallel_map(subjects, method="search")

    serial_result = serial()
    parallel_result = parallel()
    if [bool(item) for item in serial_result] != [
        bool(item) for item in parallel_result
    ]:
        raise AssertionError("parallel_map changed result order or match presence")

    serial_ms = _best_ms(serial)
    parallel_ms = _best_ms(parallel)
    print(
        f"subjects={SUBJECT_COUNT} size={SUBJECT_SIZE} runs={RUNS} "
        f"serial={serial_ms:.3f}ms parallel={parallel_ms:.3f}ms "
        f"speedup={serial_ms / parallel_ms:.2f}x"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
