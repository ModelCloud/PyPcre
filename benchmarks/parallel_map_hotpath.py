"""Measure the batched :func:`pcre.parallel_map` workload.

Run from the repository root with either interpreter::

    PYTHONPATH=. python3 benchmarks/parallel_map_hotpath.py
    PYTHONPATH=. python3.14t benchmarks/parallel_map_hotpath.py

On macOS, use the same scheduler policy for both A/B runs and use the twelve
performance-tier logical workers exposed by this host, for example::

    taskpolicy -t 1 -l 1 env PYTHONPATH=. PYPCRE_PARALLEL_WORKERS=12 python3 benchmarks/parallel_map_hotpath.py

macOS exposes the performance/efficiency cluster counts but not a portable
per-process CPU mask; the benchmark prints both counts so a run cannot be
mistaken for a hard CPU pin.

The subjects are intentionally large enough for PCRE2 to amortize worker
startup and queueing.  This reports the serial baseline, threaded execution,
and the speedup while preserving result order.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
from collections.abc import Callable

import pcre


SUBJECT_COUNT = int(os.getenv("PYPCRE_PARALLEL_SUBJECTS", "16"))
SUBJECT_SIZE = int(os.getenv("PYPCRE_PARALLEL_SIZE", "1000000"))
RUNS = int(os.getenv("PYPCRE_PARALLEL_RUNS", "5"))
WORKERS = int(os.getenv("PYPCRE_PARALLEL_WORKERS", "12"))


def _topology() -> str:
    if sys.platform != "darwin":
        return "topology=non-darwin"
    values: list[str] = []
    for name in ("hw.perflevel0.logicalcpu", "hw.perflevel1.logicalcpu"):
        try:
            value = subprocess.check_output(
                ["sysctl", "-n", name], text=True, stderr=subprocess.DEVNULL
            ).strip()
        except (OSError, subprocess.CalledProcessError):
            value = "unknown"
        values.append(value)
    return f"performance_logical={values[0]} efficiency_logical={values[1]}"


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
    parallel = lambda: pattern.parallel_map(
        subjects, method="search", max_workers=WORKERS
    )

    serial_result = serial()
    parallel_result = parallel()
    if [bool(item) for item in serial_result] != [
        bool(item) for item in parallel_result
    ]:
        raise AssertionError("parallel_map changed result order or match presence")

    serial_ms = _best_ms(serial)
    parallel_ms = _best_ms(parallel)
    print(
        f"{_topology()} subjects={SUBJECT_COUNT} size={SUBJECT_SIZE} "
        f"workers={WORKERS} runs={RUNS} "
        f"serial={serial_ms:.3f}ms parallel={parallel_ms:.3f}ms "
        f"speedup={serial_ms / parallel_ms:.2f}x"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
