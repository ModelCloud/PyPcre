# SPDX-FileCopyrightText: 2026 ModelCloud.ai
# SPDX-FileCopyrightText: 2026 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import os
import sys
import threading
import time
import unittest
from pathlib import Path
from statistics import mean

import pcre

try:
    import regex
except ImportError:  # pragma: no cover - optional dependency
    regex = None

RUN_BENCHMARKS = os.getenv("PYPCRE_RUN_BENCHMARKS") == "1"
THREAD_COUNTS = (1, 2, 4, 8, 16)
ITERATIONS_PER_THREAD = 1000
BENCHMARK_ROUNDS = int(os.getenv("PYPCRE_BENCH_ROUNDS", "5"))
REPORT_PATH = Path(__file__).with_suffix(".md")
IS_FREE_THREADED = bool(getattr(sys, "_is_gil_enabled", lambda: True)() is False)

MATCH_PATTERN = r"bench-\d+"
MATCH_SUBJECT = "bench-2026 tail"
FULLMATCH_PATTERN = r"bench-\d{4}"
FULLMATCH_SUBJECT = "bench-2026"


def _trimmed_mean(values):
    if not values:
        raise ValueError("trimmed mean requires at least one value")
    if len(values) <= 2:
        return mean(values)
    ordered = sorted(values)
    return mean(ordered[1:-1])


def _build_compile_patterns(thread_index):
    return [
        rf"^bench_{thread_index}_{iteration}_(?:[A-Z]{{2}}|\d{{2}})$"
        for iteration in range(ITERATIONS_PER_THREAD)
    ]


def _format_number(value):
    if isinstance(value, str):
        return value
    if isinstance(value, int):
        return str(value)
    return f"{value:.3f}"


def _markdown_table(rows):
    headers = list(rows[0].keys())
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(_format_number(row[header]) for header in headers) + " |")
    return "\n".join(lines)


@unittest.skipUnless(RUN_BENCHMARKS, "Set PYPCRE_RUN_BENCHMARKS=1 to enable benchmark tests")
class TestPyPcreVsRegexBenchmark(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not IS_FREE_THREADED:
            raise unittest.SkipTest("Run this benchmark with Python 3.13t free-threaded (no GIL)")
        if regex is None:
            raise unittest.SkipTest("Install regex to run the pypcre vs regex benchmark")
        if BENCHMARK_ROUNDS < 3:
            raise unittest.SkipTest("PYPCRE_BENCH_ROUNDS must be at least 3 for trimmed averages")

        cls.engines = [
            ("pypcre", pcre),
            ("regex", regex),
        ]

    def test_concurrent_api_benchmark(self):
        results = {
            "module_match": [],
            "compiled_match": [],
            "module_fullmatch": [],
            "compiled_fullmatch": [],
            "compile": [],
        }

        for thread_count in THREAD_COUNTS:
            for engine_name, module in self.engines:
                results["module_match"].append(
                    self._run_scenario(
                        api_name="module_match",
                        engine_name=engine_name,
                        module=module,
                        thread_count=thread_count,
                    )
                )
                results["compiled_match"].append(
                    self._run_scenario(
                        api_name="compiled_match",
                        engine_name=engine_name,
                        module=module,
                        thread_count=thread_count,
                    )
                )
                results["module_fullmatch"].append(
                    self._run_scenario(
                        api_name="module_fullmatch",
                        engine_name=engine_name,
                        module=module,
                        thread_count=thread_count,
                    )
                )
                results["compiled_fullmatch"].append(
                    self._run_scenario(
                        api_name="compiled_fullmatch",
                        engine_name=engine_name,
                        module=module,
                        thread_count=thread_count,
                    )
                )
                results["compile"].append(
                    self._run_scenario(
                        api_name="compile",
                        engine_name=engine_name,
                        module=module,
                        thread_count=thread_count,
                    )
                )

        report = self._build_report(results)
        REPORT_PATH.write_text(report, encoding="utf-8")
        print(f"\nBenchmark report written to {REPORT_PATH}")

        for api_name, api_results in results.items():
            self.assertEqual(len(api_results), len(self.engines) * len(THREAD_COUNTS), api_name)

    def _run_scenario(self, api_name, engine_name, module, thread_count):
        round_wall_times = []
        error_messages = []

        for _ in range(BENCHMARK_ROUNDS):
            try:
                thread_durations, wall_time = self._run_round(
                    api_name=api_name,
                    module=module,
                    thread_count=thread_count,
                )
            except Exception as exc:
                if engine_name == "regex" and api_name in {
                    "module_match",
                    "module_fullmatch",
                    "compile",
                }:
                    error_messages.append(f"{type(exc).__name__}: {exc}")
                    continue
                raise
            self.assertEqual(len(thread_durations), thread_count)
            for duration in thread_durations:
                self.assertGreaterEqual(duration, 0.0)
            self.assertGreaterEqual(wall_time, 0.0)
            round_wall_times.append(wall_time)

        if error_messages:
            return {
                "engine": engine_name,
                "threads": thread_count,
                "iterations_per_thread": ITERATIONS_PER_THREAD,
                "rounds": BENCHMARK_ROUNDS,
                "trimmed_wall_ms": "error",
                "raw_wall_ms": [],
                "error": error_messages[0],
            }

        return {
            "engine": engine_name,
            "threads": thread_count,
            "iterations_per_thread": ITERATIONS_PER_THREAD,
            "rounds": BENCHMARK_ROUNDS,
            "trimmed_wall_ms": _trimmed_mean(round_wall_times) * 1000,
            "raw_wall_ms": [value * 1000 for value in round_wall_times],
            "error": "",
        }

    def _run_round(self, api_name, module, thread_count):
        start_barrier = threading.Barrier(thread_count + 1)
        finish_barrier = threading.Barrier(thread_count + 1)
        durations = [0.0] * thread_count
        errors = []
        errors_lock = threading.Lock()

        def worker(index):
            try:
                if api_name == "module_match":
                    payloads = [MATCH_SUBJECT] * ITERATIONS_PER_THREAD
                    operation = lambda value: module.match(MATCH_PATTERN, value)
                elif api_name == "compiled_match":
                    payloads = [MATCH_SUBJECT] * ITERATIONS_PER_THREAD
                    compiled = module.compile(MATCH_PATTERN)
                    operation = compiled.match
                elif api_name == "module_fullmatch":
                    payloads = [FULLMATCH_SUBJECT] * ITERATIONS_PER_THREAD
                    operation = lambda value: module.fullmatch(FULLMATCH_PATTERN, value)
                elif api_name == "compiled_fullmatch":
                    payloads = [FULLMATCH_SUBJECT] * ITERATIONS_PER_THREAD
                    compiled = module.compile(FULLMATCH_PATTERN)
                    operation = compiled.fullmatch
                elif api_name == "compile":
                    payloads = _build_compile_patterns(index)
                    operation = module.compile
                else:  # pragma: no cover - guarded by caller
                    raise ValueError(f"Unsupported api: {api_name}")

                start_barrier.wait()
                started_at = time.perf_counter()
                for payload in payloads:
                    operation(payload)
                durations[index] = time.perf_counter() - started_at
            except BaseException as exc:  # pragma: no cover - benchmark failure path
                with errors_lock:
                    errors.append(exc)
                start_barrier.abort()
                finish_barrier.abort()
            finally:
                try:
                    finish_barrier.wait()
                except threading.BrokenBarrierError:
                    pass

        threads = [threading.Thread(target=worker, args=(index,)) for index in range(thread_count)]
        for thread in threads:
            thread.start()

        try:
            start_barrier.wait()
            wall_started_at = time.perf_counter()
            finish_barrier.wait()
            wall_time = time.perf_counter() - wall_started_at
        except threading.BrokenBarrierError:
            wall_time = 0.0

        for thread in threads:
            thread.join()

        if errors:
            raise errors[0]
        return durations, wall_time

    def _build_report(self, results):
        lines = [
            "# PyPcre vs regex benchmark",
            "",
            f"- Generated from `{Path(__file__).name}`",
            f"- Python runtime: `{sys.version.split()[0]}` free-threaded=`{str(IS_FREE_THREADED).lower()}`",
            "- APIs: `module match`, `compiled match`, `module fullmatch`, `compiled fullmatch`, `compile`",
            f"- Iterations per thread: `{ITERATIONS_PER_THREAD}`",
            f"- Benchmark rounds per scenario: `{BENCHMARK_ROUNDS}`",
            f"- Thread counts: `{', '.join(str(value) for value in THREAD_COUNTS)}`",
            "- Timing excludes thread startup by synchronizing all worker threads before the measured region.",
            "- `pypcre (ms)` / `regex (ms)` are each-round averages: wall-clock elapsed time per round, with the highest and lowest rounds removed before averaging.",
            "- `compiled match` / `compiled fullmatch` benchmark `c = compile(...); c.match(...) / c.fullmatch(...)`, with compile done before timing in each worker thread.",
            "- `module match` / `module fullmatch` benchmark direct module calls and therefore include each engine's internal compile/cache path.",
            "- `regex` free-threaded failures in uncompiled and compile paths are recorded as `error` instead of failing the whole benchmark.",
            "- `pypcre_vs_regex` shows `faster xx.x%`, `slower xx.x%`, `same`, or `n/a` based on wall-clock time.",
            "",
        ]

        section_titles = {
            "module_match": f"module match ({ITERATIONS_PER_THREAD} times avg)",
            "compiled_match": f"compiled match ({ITERATIONS_PER_THREAD} times avg)",
            "module_fullmatch": f"module fullmatch ({ITERATIONS_PER_THREAD} times avg)",
            "compiled_fullmatch": f"compiled fullmatch ({ITERATIONS_PER_THREAD} times avg)",
            "compile": f"compile ({ITERATIONS_PER_THREAD} times avg)",
        }
        for api_name in (
                "module_match",
                "compiled_match",
                "module_fullmatch",
                "compiled_fullmatch",
                "compile",
        ):
            lines.append(f"## {section_titles[api_name]}")
            lines.append("")
            lines.append(
                _markdown_table(self._report_rows_for_api(results[api_name]))
            )
            error_lines = self._report_errors_for_api(results[api_name])
            if error_lines:
                lines.append("")
                lines.extend(error_lines)
            lines.append("")

        return "\n".join(lines)

    def _report_rows_for_api(self, api_results):
        rows = []
        for thread_count in THREAD_COUNTS:
            pypcre_result = next(
                row for row in api_results if row["engine"] == "pypcre" and row["threads"] == thread_count
            )
            regex_result = next(
                row for row in api_results if row["engine"] == "regex" and row["threads"] == thread_count
            )
            rows.append(
                {
                    "threads": thread_count,
                    "pypcre (ms)": pypcre_result["trimmed_wall_ms"],
                    "regex (ms)": regex_result["trimmed_wall_ms"],
                    "pypcre_vs_regex": self._comparison_label(
                        pypcre_result["trimmed_wall_ms"], regex_result["trimmed_wall_ms"]
                    ),
                }
            )
        return rows

    def _report_errors_for_api(self, api_results):
        errors = [
            row for row in api_results
            if row["engine"] == "regex" and row["error"]
        ]
        if not errors:
            return []
        lines = ["regex errors:"]
        for row in errors:
            lines.append(f"- threads={row['threads']}: {row['error']}")
        return lines

    def _comparison_label(self, pypcre_value, regex_value):
        if not isinstance(pypcre_value, (int, float)) or not isinstance(regex_value, (int, float)):
            return "n/a"
        if regex_value == 0:
            return "n/a"
        pct = ((regex_value - pypcre_value) / regex_value) * 100.0
        if abs(pct) < 0.05:
            return "same"
        if pct > 0:
            return f"faster {pct:.1f}%"
        return f"slower {abs(pct):.1f}%"


if __name__ == "__main__":
    unittest.main()
