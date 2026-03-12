# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import collections
import os
import sys
import threading
import time
import unittest
from statistics import mean, median

import pcre_ext_c

import pcre

try:
    import regex as external_regex
except ImportError:  # pragma: no cover - optional dependency
    external_regex = None

try:
    from tabulate import tabulate
except ImportError:  # pragma: no cover - optional dependency
    def tabulate(rows, headers="keys", floatfmt=".3f", tablefmt="github"):
        if headers != "keys" or tablefmt != "github":
            raise ValueError("fallback tabulate only supports headers='keys' and tablefmt='github'")
        if not rows:
            return ""

        columns = list(rows[0].keys())

        def _format_cell(value):
            if isinstance(value, float):
                return format(value, floatfmt)
            return str(value)

        rendered_rows = [[_format_cell(row.get(column, "")) for column in columns] for row in rows]
        widths = [
            max(len(str(column)), *(len(rendered_row[index]) for rendered_row in rendered_rows))
            for index, column in enumerate(columns)
        ]

        def _render_row(values):
            return "| " + " | ".join(value.ljust(widths[index]) for index, value in enumerate(values)) + " |"

        header_row = _render_row([str(column) for column in columns])
        separator_row = "| " + " | ".join("-" * width for width in widths) + " |"
        data_rows = [_render_row(values) for values in rendered_rows]
        return "\n".join([header_row, separator_row, *data_rows])

RUN_BENCHMARKS = os.getenv("PYPCRE_RUN_BENCHMARKS") == "1"
BENCHMARK_OUTPUT_PATH = os.getenv("PYPCRE_BENCH_OUTPUT", "tests/test_bench_threads.md")
THREAD_ITERATIONS = int(os.getenv("PYPCRE_BENCH_THREAD_ITERS", "40"))
BENCHMARK_OUTPUT_INITIALIZED_ENV = "PYPCRE_BENCH_OUTPUT_INITIALIZED"

PATTERN_CASES = [
    (r"\bfoo\b", ["foo bar foo", "prefix foo suffix", "no match here"]),
    (r"(?P<word>[A-Za-z]+)", ["Hello world", "Another Line", "lower CASE"]),
    (r"(?:(?<=foo)bar|baz)(?!qux)", ["foobar", "foobaz", "foobazqux"]),
]


def _parse_thread_scenarios():
    raw = os.getenv("PYPCRE_BENCH_THREADS", "1,2,4,8")
    values = []
    for chunk in raw.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        value = int(chunk)
        if value <= 0:
            raise ValueError("PYPCRE_BENCH_THREADS must contain positive integers")
        values.append(value)
    if not values:
        raise ValueError("PYPCRE_BENCH_THREADS did not contain any thread counts")
    return tuple(dict.fromkeys(values))


THREAD_SCENARIOS = _parse_thread_scenarios()
BENCHMARK_OUTPUT_LINES = []


def _benchmark_print(message=""):
    print(message)
    BENCHMARK_OUTPUT_LINES.append(f"{message}\n")


def _flush_benchmark_output():
    if not RUN_BENCHMARKS:
        return
    with open(BENCHMARK_OUTPUT_PATH, "a", encoding="utf-8") as handle:
        handle.writelines(BENCHMARK_OUTPUT_LINES)


def _build_compiled_operations(pattern):
    operations = {}
    if hasattr(pattern, "match"):
        method = pattern.match
        operations["match"] = lambda text, method=method: method(text)
    if hasattr(pattern, "search"):
        method = pattern.search
        operations["search"] = lambda text, method=method: method(text)
    return operations


def _describe_pattern_scenario(subject_kind: str, pattern_text, op_name: str) -> str:
    return f"{subject_kind} pattern {pattern_text} using compiled-pattern {op_name} call"


def _gil_is_enabled() -> bool:
    return bool(hasattr(sys, "_is_gil_enabled") and sys._is_gil_enabled())


@unittest.skipUnless(RUN_BENCHMARKS, "Set PYPCRE_RUN_BENCHMARKS=1 to enable benchmark tests")
class TestRegexThreadBenchmarks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        BENCHMARK_OUTPUT_LINES.clear()
        if RUN_BENCHMARKS and os.getenv(BENCHMARK_OUTPUT_INITIALIZED_ENV) != "1":
            with open(BENCHMARK_OUTPUT_PATH, "w", encoding="utf-8") as handle:
                handle.write("")
            os.environ[BENCHMARK_OUTPUT_INITIALIZED_ENV] = "1"

        def _compile_pcre(pattern):
            return pcre.compile(pattern)

        def _compile_pypcre_backend(pattern):
            flags = pcre_ext_c.PCRE2_UTF | pcre_ext_c.PCRE2_UCP if isinstance(pattern, str) else 0
            try:
                return pcre_ext_c.compile(pattern, flags)
            except Exception:
                return None

        def _compile_regex(pattern):
            if external_regex is None:
                return None
            try:
                return external_regex.compile(pattern)
            except Exception:
                return None

        cls.engines = [
            ("PyPcre", pcre, _compile_pcre),
            ("PyPcre backend", pcre_ext_c, _compile_pypcre_backend),
        ]
        if external_regex is not None:
            cls.engines.append(("regex", external_regex, _compile_regex))

        if THREAD_ITERATIONS <= 0:
            raise unittest.SkipTest("Thread iterations must be positive for meaningful benchmarks")
        if any(thread_count <= 0 for thread_count in THREAD_SCENARIOS):
            raise unittest.SkipTest("Thread scenarios must be positive for meaningful benchmarks")

    @classmethod
    def tearDownClass(cls):
        _flush_benchmark_output()

    def test_multi_threaded_match(self):
        if _gil_is_enabled():
            self.skipTest("multi-thread benchmark requires Python running with the GIL disabled")
        pattern_text, subjects = PATTERN_CASES[0]
        results_by_combo = collections.defaultdict(list)
        for engine_name, _, compile_fn in self.engines:
            compiled = compile_fn(pattern_text)
            compiled_ops = _build_compiled_operations(compiled)
            if "search" in compiled_ops:
                op_name = "search"
            elif "match" in compiled_ops:
                op_name = "match"
            else:
                self.skipTest(f"{engine_name} does not provide match or search for multi-thread benchmark")
            operation = compiled_ops[op_name]

            for thread_count in THREAD_SCENARIOS:
                with self.subTest(engine=engine_name, operation=op_name, threads=thread_count):
                    thread_times, total_elapsed = self._run_thread_benchmark(
                        operation,
                        subjects,
                        thread_count,
                    )
                    self.assertEqual(len(thread_times), thread_count)
                    for duration in thread_times:
                        self.assertGreaterEqual(duration, 0.0)
                    results_by_combo[(pattern_text, op_name)].append(
                        {
                            "engine": engine_name,
                            "threads": thread_count,
                            "calls_per_thread": THREAD_ITERATIONS * len(subjects),
                            "total_calls": thread_count * THREAD_ITERATIONS * len(subjects),
                            "min_ms": min(thread_times) * 1000,
                            "median_ms": median(thread_times) * 1000,
                            "max_ms": max(thread_times) * 1000,
                            "mean_ms": mean(thread_times) * 1000,
                            "total_ms": total_elapsed * 1000,
                        }
                    )

        self._emit_thread_results("Benchmark: multi-thread text search", results_by_combo, self.engines)

    def _emit_thread_results(self, title, results_by_combo, engines):
        if not results_by_combo:
            return
        engine_order = {engine_name: index for index, (engine_name, _, _) in enumerate(engines)}
        _benchmark_print(f"\n{title}:")
        for (pattern_text, op_name) in sorted(results_by_combo):
            result_rows = sorted(
                results_by_combo[(pattern_text, op_name)],
                key=lambda row: (
                    row["threads"],
                    engine_order.get(row["engine"], len(engine_order)),
                    row["total_ms"],
                ),
            )
            _benchmark_print(f"\nScenario: {_describe_pattern_scenario('Text', pattern_text, op_name)}")
            for thread_count in THREAD_SCENARIOS:
                scenario_rows = [row for row in result_rows if row["threads"] == thread_count]
                present_engines = {row["engine"] for row in scenario_rows}
                for engine_name, _, _ in engines:
                    if engine_name not in present_engines:
                        scenario_rows.append(
                            {
                                "engine": engine_name,
                                "threads": thread_count,
                                "calls_per_thread": "n/a",
                                "total_calls": "n/a",
                                "min_ms": "n/a",
                                "median_ms": "n/a",
                                "max_ms": "n/a",
                                "mean_ms": "n/a",
                                "total_ms": "n/a",
                            }
                        )
                scenario_rows.sort(
                    key=lambda row: (
                        engine_order.get(row["engine"], len(engine_order)),
                        row["total_ms"] if isinstance(row["total_ms"], (int, float)) else float("inf"),
                    )
                )
                best_metric = min(
                    (
                        row["total_ms"]
                        for row in scenario_rows
                        if isinstance(row.get("total_ms"), (int, float))
                    ),
                    default=None,
                )
                display_rows = []
                for row in scenario_rows:
                    display_row = dict(row)
                    if isinstance(row.get("total_ms"), (int, float)) and best_metric not in (None, 0):
                        diff_pct = ((row["total_ms"] - best_metric) / best_metric) * 100
                        display_row["diff_pct"] = f"{diff_pct:+.1f}%"
                    elif isinstance(row.get("total_ms"), (int, float)):
                        display_row["diff_pct"] = "0.0%"
                    else:
                        display_row["diff_pct"] = "n/a"
                    display_row["best"] = (
                        "*"
                        if best_metric is not None and isinstance(row.get("total_ms"), (int, float)) and row["total_ms"] == best_metric
                        else ""
                    )
                    display_rows.append(display_row)
                _benchmark_print(f"\nThreads: {thread_count}")
                _benchmark_print()
                _benchmark_print(
                    tabulate(
                        display_rows,
                        headers="keys",
                        floatfmt=".3f",
                        tablefmt="github",
                    )
                )
        _benchmark_print("\n* best = lowest total_ms; diff_pct = percentage slower than best")

    def _run_thread_benchmark(self, operation, subjects, thread_count):
        start_barrier = threading.Barrier(thread_count + 1)
        finish_barrier = threading.Barrier(thread_count + 1)
        durations = [0.0] * thread_count

        def worker(index: int):
            start_barrier.wait()
            start_time = time.perf_counter()
            for _ in range(THREAD_ITERATIONS):
                for subject in subjects:
                    operation(subject)
            durations[index] = time.perf_counter() - start_time
            finish_barrier.wait()

        threads = [threading.Thread(target=worker, args=(idx,)) for idx in range(thread_count)]
        for thread in threads:
            thread.start()

        start_barrier.wait()
        global_start = time.perf_counter()
        finish_barrier.wait()
        total_elapsed = time.perf_counter() - global_start

        for thread in threads:
            thread.join()

        self.assertGreaterEqual(total_elapsed, 0.0)
        return durations, total_elapsed


if __name__ == "__main__":
    unittest.main()
