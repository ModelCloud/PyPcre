# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Iterable, Tuple

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "experiments" / "bench_cache_module.py"
BASELINE_PATH = ROOT / "experiments" / "cache_baseline.json"
CURRENT_PATH = ROOT / "experiments" / "cache_current.json"

THREAD_COUNTS = (1, 2, 4, 8)


def run_bench(kind: str, strategy: str, threads: int, iterations: int, gil: str) -> dict[str, object]:
    env = os.environ.copy()
    env.setdefault("PYTHON_GIL", gil)
    cmd = [sys.executable, str(SCRIPT), kind, strategy, str(threads), str(iterations)]
    completed = subprocess.run(cmd, check=True, capture_output=True, text=True, env=env)
    return json.loads(completed.stdout)


def collect(results: Iterable[Tuple[str, str, int]], iterations: int, gil: str) -> list[dict[str, object]]:
    rows = []
    for kind, strategy, threads in results:
        rows.append(run_bench(kind, strategy, threads, iterations, gil))
    return rows


def compute_deltas(baseline: list[dict[str, object]], current: list[dict[str, object]]) -> list[dict[str, object]]:
    base_map = {
        (row["kind"], row["strategy"], row["threads"]): row["per_call_ns"]
        for row in baseline
    }
    deltas: list[dict[str, object]] = []
    for row in current:
        key = (row["kind"], row["strategy"], row["threads"])
        base = base_map.get(key)
        if base is None:
            delta_pct = None
        else:
            delta_pct = ((row["per_call_ns"] - base) / base) * 100
        deltas.append(dict(row, baseline_per_call_ns=base, delta_pct=delta_pct))
    return deltas


def format_table(rows: list[dict[str, object]]) -> str:
    headers = ["kind", "strategy", "threads", "per_call_ns", "baseline", "delta %"]
    table_rows = []
    for row in rows:
        table_rows.append([
            row["kind"],
            row["strategy"],
            str(row["threads"]),
            f"{row['per_call_ns']:.1f}",
            "-" if row["baseline_per_call_ns"] is None else f"{row['baseline_per_call_ns']:.1f}",
            "-" if row["delta_pct"] is None else f"{row['delta_pct']:+.2f}%",
        ])
    widths = [max(len(r[i]) for r in ([headers] + table_rows)) for i in range(len(headers))]
    def fmt(row: list[str]) -> str:
        return " | ".join(word.ljust(widths[i]) for i, word in enumerate(row))
    lines = [fmt(headers), "-+-".join("-" * w for w in widths)]
    lines.extend(fmt(row) for row in table_rows)
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Benchmark the pcre.cache module against stored baselines.")
    parser.add_argument("kind", choices=["c", "py", "both"], help="Which cache implementation to benchmark")
    parser.add_argument("--iterations", type=int, default=20000, help="Iterations per worker (default: 20000)")
    parser.add_argument("--gil", choices=["0", "1"], default=os.environ.get("PYTHON_GIL", "0"), help="PYTHON_GIL value for subprocesses")
    parser.add_argument("--update-baseline", action="store_true", help="Overwrite the stored baseline with the current results")
    parser.add_argument("--no-compare", action="store_true", help="Skip delta computation")
    args = parser.parse_args()

    if args.kind == "both":
        run_spec = [(kind, strategy, threads) for kind in ("c", "py") for strategy in ("thread-local", "global") for threads in THREAD_COUNTS]
    else:
        run_spec = [(args.kind, strategy, threads) for strategy in ("thread-local", "global") for threads in THREAD_COUNTS]

    current_rows = collect(run_spec, args.iterations, args.gil)
    CURRENT_PATH.write_text(json.dumps({"iterations": args.iterations, "results": current_rows}, indent=2) + "\n")

    if args.update_baseline:
        BASELINE_PATH.write_text(json.dumps({"iterations": args.iterations, "results": current_rows}, indent=2) + "\n")

    if not args.no_compare and BASELINE_PATH.exists():
        baseline_rows = json.loads(BASELINE_PATH.read_text())
        deltas = compute_deltas(baseline_rows["results"], current_rows)
        print(format_table(deltas))
    else:
        print(format_table([dict(row, baseline_per_call_ns=None, delta_pct=None) for row in current_rows]))


if __name__ == "__main__":
    main()
