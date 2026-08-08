"""
Head-to-head ``sub`` benchmark of PyPcre vs stdlib ``re`` and the ``regex`` package.

Run with:
    python3 benchmarks/sub_bench.py

Each workload replaces 100,000 tokens. Times are the best of several runs;
lower is better. Compiled patterns are reused and PyPcre JIT is enabled by
default where applicable.
"""

from __future__ import annotations

import re as stdlib_re
import statistics
import sys
import time
from typing import Any

try:
    import regex
except ImportError:  # pragma: no cover - optional competitor
    regex = None  # type: ignore[assignment]

import pcre


def _best_ms(fn: Any, runs: int = 7) -> float:
    times: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        fn()
        times.append((time.perf_counter() - start) * 1000.0)
    return min(times)


def _bench_sub(label: str, pattern: str, repl: str, text: str) -> dict[str, float | None]:
    re_pat = stdlib_re.compile(pattern)
    re_time = _best_ms(lambda: re_pat.sub(repl, text))

    regex_time: float | None = None
    if regex is not None:
        regex_pat = regex.compile(pattern)
        regex_time = _best_ms(lambda: regex_pat.sub(repl, text))

    pc_pat = pcre.compile(pattern)
    pc_time = _best_ms(lambda: pc_pat.sub(repl, text))

    return {
        "label": label,
        "re": re_time,
        "regex": regex_time,
        "pcre": pc_time,
    }


def main() -> int:
    text = " ".join(f"w{i}" for i in range(100_000))

    rows: list[dict[str, Any]] = [
        _bench_sub("Literal replacement", r"\w+", "[X]", text),
        _bench_sub("Single numeric backref", r"(w)\d+", r"[\1]", text),
        _bench_sub("Two numeric backrefs", r"(w)(\d+)", r"\2-\1", text),
        _bench_sub("Named backref", r"(?P<g>\w+)", r"<\g<g>>", text),
    ]

    print("\n| Workload | re (ms) | regex (ms) | PyPcre (ms) | edge vs re | edge vs regex |")
    print("| --- | ---: | ---: | ---: | ---: | ---: |")
    for row in rows:
        re_t = row["re"]
        regex_t = row["regex"]
        pc_t = row["pcre"]
        vs_re = f"{re_t / pc_t:.2f}x" if pc_t else "n/a"
        vs_regex = f"{regex_t / pc_t:.2f}x" if regex_t and pc_t else "n/a"
        regex_cell = f"{regex_t:.3f}" if regex_t is not None else "-"
        print(
            f"| {row['label']} | {re_t:.3f} | {regex_cell} | "
            f"{pc_t:.3f} | {vs_re} | {vs_regex} |"
        )

    winners = [
        r
        for r in rows
        if r["pcre"]
        and (r["re"] / r["pcre"] > 2.0 or (r["regex"] and r["regex"] / r["pcre"] > 2.0))
    ]
    print(f"\nPyPcre is >2x faster on {len(winners)} of {len(rows)} workloads.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
