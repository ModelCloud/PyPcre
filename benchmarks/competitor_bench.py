"""
Head-to-head benchmark of PyPcre vs stdlib `re` and the `regex` package.

Run with:
    python3 benchmarks/competitor_bench.py

Each workload is timed as the best of several runs and uses compiled patterns
with the appropriate flags for multiline/anchored scans.
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


def _best_ms(fn: Any, runs: int = 7, setup: Any | None = None) -> float:
    times: list[float] = []
    for _ in range(runs):
        if setup:
            setup()
        start = time.perf_counter()
        fn()
        times.append((time.perf_counter() - start) * 1000.0)
    return min(times)


def _make_text(kind: str, n: int) -> str:
    if kind == "log":
        levels = ["INFO", "DEBUG", "WARN", "ERROR"]
        lines = [f"2025-01-01 12:00:00 {levels[i % 4]} message {i} details here" for i in range(n)]
        return "\n".join(lines) + "\n"
    if kind == "names":
        lines = [f"First{i} Last{i} <email{i}@example.com>" for i in range(n)]
        return "\n".join(lines) + "\n"
    if kind == "lookaround":
        return " ".join(
            ["foo bar" if i % 3 == 0 else "baz" if i % 3 == 1 else "other" for i in range(n)]
        )
    raise ValueError(kind)


def _bench(label: str, pattern: str, text: str, flags: int = 0, pcre_flags: int = 0) -> dict[str, float | None]:
    re_pat = stdlib_re.compile(pattern, flags)
    re_fn = lambda: re_pat.findall(text)  # noqa: E731
    re_time = _best_ms(re_fn)

    regex_time: float | None = None
    if regex is not None:
        regex_pat = regex.compile(pattern, flags)
        regex_time = _best_ms(lambda: regex_pat.findall(text))

    pc_pat = pcre.compile(pattern, pcre_flags)
    pc_time = _best_ms(lambda: pc_pat.findall(text))

    return {
        "label": label,
        "re": re_time,
        "regex": regex_time,
        "pcre": pc_time,
    }


def main() -> int:
    rows: list[dict[str, Any]] = []

    # Workload 1: multiline anchored extraction of log severity lines.
    log_text = _make_text("log", 100_000)
    rows.append(_bench(
        "Extract WARN/ERROR lines",
        r"^(?:WARN|ERROR).*?$",
        log_text,
        flags=stdlib_re.MULTILINE,
        pcre_flags=pcre.Flag.MULTILINE,
    ))

    # Workload 2: multiline anchored full-name extraction.
    name_text = _make_text("names", 100_000)
    rows.append(_bench(
        "Full-name per line",
        r"^[A-Z][a-z]+ [A-Z][a-z]+",
        name_text,
        flags=stdlib_re.MULTILINE,
        pcre_flags=pcre.Flag.MULTILINE,
    ))

    # Workload 3: lookaround-heavy token scan on a large single-line buffer.
    look_text = _make_text("lookaround", 100_000)
    rows.append(_bench(
        "Lookbehind + negative lookahead",
        r"(?:(?<=foo)bar|baz)(?!qux)",
        look_text,
    ))

    # Print a table.
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

    # Recommend only including rows with a clear win.
    winners = [r for r in rows if r["pcre"] and (r["re"] / r["pcre"] > 2.0 or (r["regex"] and r["regex"] / r["pcre"] > 2.0))]
    print(f"\nPyPcre is >2x faster on {len(winners)} of {len(rows)} workloads.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
