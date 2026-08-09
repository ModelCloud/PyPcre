"""
Free-threaded head-to-head benchmark of PyPcre vs stdlib ``re`` and ``regex``.

Run with:
    python3 benchmarks/free_threaded_bench.py

Each workload is split into 8 chunks and processed in parallel by a
``ThreadPoolExecutor`` (8 workers). Times are the best of several runs; lower is
better.
"""

from __future__ import annotations

import concurrent.futures
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
from pcre import Flag


WORKERS = 8


def _best_ms(fn: Any, runs: int = 7) -> float:
    times: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        fn()
        times.append((time.perf_counter() - start) * 1000.0)
    return min(times)


def _make_text(kind: str, n: int) -> str:
    if kind == "log":
        levels = ["INFO", "DEBUG", "WARN", "ERROR"]
        lines = [
            f"2025-01-01 12:00:00 {levels[i % 4]} message {i} details here"
            for i in range(n)
        ]
        return "\n".join(lines) + "\n"
    if kind == "names":
        lines = [f"First{i} Last{i} <email{i}@example.com>" for i in range(n)]
        return "\n".join(lines) + "\n"
    raise ValueError(kind)


def _chunk(text: str, chunks: int) -> list[str]:
    size = len(text) // chunks
    return [
        text[i * size : (i + 1) * size if i < chunks - 1 else len(text)]
        for i in range(chunks)
    ]


def _bench(
    label: str,
    pattern: str,
    text: str,
    flags: int = 0,
    pcre_flags: int = 0,
) -> dict[str, Any]:
    chunks = _chunk(text, WORKERS)

    pc_pat = pcre.compile(pattern, pcre_flags | Flag.THREADS)

    def _pc_fn() -> list[Any]:
        with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as executor:
            return list(executor.map(pc_pat.findall, chunks))

    re_pat = stdlib_re.compile(pattern, flags)

    def _re_fn() -> list[Any]:
        with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as executor:
            return list(executor.map(re_pat.findall, chunks))

    regex_time: float | None = None
    if regex is not None:
        rx_pat = regex.compile(pattern, flags)

        def _regex_fn() -> list[Any]:
            with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as executor:
                return list(executor.map(rx_pat.findall, chunks))

        regex_time = _best_ms(_regex_fn)

    return {
        "label": label,
        "pcre": _best_ms(_pc_fn),
        "re": _best_ms(_re_fn),
        "regex": regex_time,
    }


def main() -> int:
    rows: list[dict[str, Any]] = [
        _bench(
            "Extract WARN/ERROR lines",
            r"^(?:WARN|ERROR).*?$",
            _make_text("log", 100_000),
            flags=stdlib_re.MULTILINE,
            pcre_flags=Flag.MULTILINE,
        ),
        _bench(
            "Per-line full-name extraction",
            r"^[A-Z][a-z]+ [A-Z][a-z]+",
            _make_text("names", 100_000),
            flags=stdlib_re.MULTILINE,
            pcre_flags=Flag.MULTILINE,
        ),
    ]

    print("\n| Workload | PyPcre (ms) | re (ms) | regex (ms) | PyPcre edge |")
    print("| --- | ---: | ---: | ---: | --- |")
    for row in rows:
        re_t = row["re"]
        regex_t = row["regex"]
        pc_t = row["pcre"]
        vs_re = f"{re_t / pc_t:.1f}x" if pc_t else "n/a"
        vs_regex = f"{regex_t / pc_t:.1f}x" if regex_t and pc_t else "n/a"
        regex_cell = f"{regex_t:.3f}" if regex_t is not None else "-"
        print(
            f"| {row['label']} | {pc_t:.3f} | {re_t:.3f} | {regex_cell} | "
            f"**{vs_re}** vs re, **{vs_regex}** vs regex |"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
