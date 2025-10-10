# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import collections
import os
import re
import threading
import time
import unittest
from statistics import mean, median

import pcre


try:
    import pcre2 as external_pcre2
except ImportError:  # pragma: no cover - optional dependency
    external_pcre2 = None

try:
    import regex as external_regex
except ImportError:  # pragma: no cover - optional dependency
    external_regex = None

from tabulate import tabulate


RUN_BENCHMARKS = os.getenv("PYPCRE_RUN_BENCHMARKS") == "1"
THREAD_COUNT = int(os.getenv("PYPCRE_BENCH_THREADS", "16"))
SINGLE_ITERATIONS = int(os.getenv("PYPCRE_BENCH_ITERS", "5000"))
THREAD_ITERATIONS = int(os.getenv("PYPCRE_BENCH_THREAD_ITERS", "40"))


UNICODE_SAMPLE_LENGTH = 128
UNICODE_VARIANT_BASES = {
    "ascii": "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
    "latin-1": "\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf",
    "2byte": "\u0100\u0102\u0104\u0106\u0108\u010a\u010c\u010e\u0110\u0112\u0114\u0116\u0118\u011a\u011c\u011e\u0120\u0122\u0124\u0126\u0128\u012a\u012c\u012e\u0130\u0134\u0136\u0139\u013b\u013d\u013f\u0141\u0143\u0145\u0147\u014a",
    "3byte": "\u6f22\u5b57\u4eee\u540d\u4ea4\u932f\u7e41\u9ad4\u5b57\u6d4b\u8bd5\u7de8\u78bc\u8cc7\u6599",
    "4byte": "\U0001f600\U0001f601\U0001f602\U0001f923\U0001f603\U0001f604\U0001f605\U0001f606\U0001f607\U0001f608\U0001f609\U0001f60a\U0001f60b\U0001f60c\U0001f60d\U0001f60e",
}


def _expand_unicode_variants():
    subjects = {}
    for label, base in UNICODE_VARIANT_BASES.items():
        repeats = (UNICODE_SAMPLE_LENGTH // len(base)) + 1
        primary = (base * repeats)[:UNICODE_SAMPLE_LENGTH]
        rotation = min(len(primary), len(base))
        rotated = primary[rotation:] + primary[:rotation]
        mirrored = primary[::-1]
        subjects[label] = [primary, rotated, mirrored]
    return subjects


UNICODE_VARIANT_SUBJECTS = _expand_unicode_variants()


PATTERN_CASES = [
    (r"foo", ["foo bar foo", "prefix foo suffix", "no match here"]),
    (r"(?P<word>[A-Za-z]+)", ["Hello world", "Another Line", "lower CASE"]),
    (r"(?:(?<=foo)bar|baz)(?!qux)", ["foobar", "foobaz", "foobazqux"]),
]


def _build_compiled_operations(pattern):
    operations = {}
    if hasattr(pattern, "match"):
        method = pattern.match
        operations["match"] = lambda text, method=method: method(text)
    if hasattr(pattern, "search"):
        method = pattern.search
        operations["search"] = lambda text, method=method: method(text)
    if hasattr(pattern, "fullmatch"):
        method = pattern.fullmatch
        operations["fullmatch"] = lambda text, method=method: method(text)
    if hasattr(pattern, "findall"):
        method = pattern.findall
        operations["findall"] = lambda text, method=method: method(text)
    if hasattr(pattern, "finditer"):
        method = pattern.finditer
        operations["finditer"] = lambda text, method=method: collections.deque(method(text), maxlen=0)
    return operations


def _build_module_operations(module):
    operations = {}
    for name in ("match", "search", "fullmatch", "findall", "finditer"):
        func = getattr(module, name, None)
        if func is None:
            continue
        if name == "finditer":
            operations[f"module_{name}"] = lambda pattern, text, func=func: collections.deque(func(pattern, text), maxlen=0)
        else:
            operations[f"module_{name}"] = lambda pattern, text, func=func: func(pattern, text)
    return operations


@unittest.skipUnless(RUN_BENCHMARKS, "Set PYPCRE_RUN_BENCHMARKS=1 to enable benchmark tests")
class TestRegexBenchmarks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        def _compile_pcre(pattern: str):
            return pcre.compile(pattern)

        def _compile_re(pattern: str):
            try:
                return re.compile(pattern)
            except re.error:
                return None

        def _compile_pcre2(pattern: str):
            if external_pcre2 is None:
                return None
            try:
                return external_pcre2.compile(pattern, flags=getattr(external_pcre2, 'UTF', 0) | getattr(external_pcre2, 'UCP', 0))
            except Exception:
                return None

        def _compile_regex(pattern: str):
            if external_regex is None:
                return None
            try:
                return external_regex.compile(pattern, flags=getattr(external_regex, 'UNICODE', 0) | getattr(external_regex, 'FULLCASE', 0))
            except Exception:
                return None

        cls.engines = [
            ("re", re, _compile_re),
            ("pcre", pcre, _compile_pcre),
        ]
        if external_pcre2 is not None:
            cls.engines.append(("pcre2", external_pcre2, _compile_pcre2))
        if external_regex is not None:
            cls.engines.append(("regex", external_regex, _compile_regex))
        if SINGLE_ITERATIONS <= 0 or THREAD_ITERATIONS <= 0:
            raise unittest.SkipTest("Iterations must be positive for meaningful benchmarks")

    def test_single_thread_patterns(self):
        results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            for pattern_text, subjects in PATTERN_CASES:
                compiled = compile_fn(pattern_text)
                if compiled is None:
                    continue
                compiled_ops = _build_compiled_operations(compiled)
                expected_calls = SINGLE_ITERATIONS * len(subjects)

                for op_name, operation in compiled_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(pattern_text, op_name)].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                                "per_call_ns": (elapsed / expected_calls) * 1e9,
                            }
                        )

                for op_name, operation in module_ops.items():
                    with self.subTest(engine=engine_name, pattern=pattern_text, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(pattern_text, subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(pattern_text, op_name)].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                                "per_call_ns": (elapsed / expected_calls) * 1e9,
                            }
                        )

        if results_by_combo:
            print("\nSingle-thread benchmark results:")
            for (pattern_text, op_name) in sorted(results_by_combo):
                result_rows = sorted(
                    results_by_combo[(pattern_text, op_name)],
                    key=lambda row: row["total_ms"],
                )
                present_engines = {row["engine"] for row in result_rows}
                for engine_name, _, _ in self.engines:
                    if engine_name not in present_engines:
                        result_rows.append(
                            {
                                "engine": engine_name,
                                "calls": "n/a",
                                "total_ms": "n/a",
                                "per_call_ns": "n/a",
                            }
                        )
                result_rows.sort(key=lambda row: row["total_ms"] if isinstance(row["total_ms"], (int, float)) else float("inf"))
                print(f"\nPattern: {pattern_text} | Operation: {op_name}")
                print(
                    tabulate(
                        result_rows,
                        headers="keys",
                        floatfmt=".3f",
                        tablefmt="github",
                    )
                )


    def test_character_width_subjects(self):
        pattern_text = r".+"
        results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            compiled = compile_fn(pattern_text)
            if compiled is None:
                continue
            compiled_ops = _build_compiled_operations(compiled)
            for variant_label, subjects in UNICODE_VARIANT_SUBJECTS.items():
                expected_calls = SINGLE_ITERATIONS * len(subjects)
                for op_name, operation in compiled_ops.items():
                    with self.subTest(engine=engine_name, variant=variant_label, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(variant_label, op_name)].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                                "per_call_ns": (elapsed / expected_calls) * 1e9,
                            }
                        )
                for op_name, operation in module_ops.items():
                    with self.subTest(engine=engine_name, variant=variant_label, operation=op_name):
                        call_count = 0
                        start = time.perf_counter()
                        for _ in range(SINGLE_ITERATIONS):
                            for subject in subjects:
                                operation(pattern_text, subject)
                                call_count += 1
                        elapsed = time.perf_counter() - start
                        self.assertEqual(call_count, expected_calls)
                        self.assertGreaterEqual(elapsed, 0.0)
                        results_by_combo[(variant_label, op_name)].append(
                            {
                                "engine": engine_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                                "per_call_ns": (elapsed / expected_calls) * 1e9,
                            }
                        )
        if results_by_combo:
            print("\nUnicode width benchmark results:")
            for (variant_label, op_name) in sorted(results_by_combo):
                result_rows = sorted(
                    results_by_combo[(variant_label, op_name)],
                    key=lambda row: row["total_ms"],
                )
                present_engines = {row["engine"] for row in result_rows}
                for engine_name, _, _ in self.engines:
                    if engine_name not in present_engines:
                        result_rows.append(
                            {
                                "engine": engine_name,
                                "calls": "n/a",
                                "total_ms": "n/a",
                                "per_call_ns": "n/a",
                            }
                        )
                result_rows.sort(
                    key=lambda row: row["total_ms"] if isinstance(row["total_ms"], (int, float)) else float("inf")
                )
                print(f"\nVariant: {variant_label} | Operation: {op_name}")
                print(
                    tabulate(
                        result_rows,
                        headers="keys",
                        floatfmt=".3f",
                        tablefmt="github",
                    )
                )

    def test_multi_threaded_match(self):
        pattern_text, subjects = PATTERN_CASES[0]
        results_by_combo = collections.defaultdict(list)
        for engine_name, module, compile_fn in self.engines:
            if engine_name == "regex":
                # The third-party regex engine is not guaranteed GIL=0 safe, so keep it single-threaded.
                continue
            compiled = compile_fn(pattern_text)
            compiled_ops = _build_compiled_operations(compiled)
            if "search" in compiled_ops:
                op_name = "search"
            elif "match" in compiled_ops:
                op_name = "match"
            else:
                self.skipTest(f"{engine_name} does not provide match or search for multi-thread benchmark")
            operation = compiled_ops[op_name]
            subjects_cycle = subjects

            with self.subTest(engine=engine_name, operation=op_name):
                thread_times, total_elapsed = self._run_thread_benchmark(operation, subjects_cycle)
                self.assertEqual(len(thread_times), THREAD_COUNT)
                for duration in thread_times:
                    self.assertGreaterEqual(duration, 0.0)
                results_by_combo[(pattern_text, op_name)].append(
                    {
                        "engine": engine_name,
                        "threads": THREAD_COUNT,
                        "min_ms": min(thread_times) * 1000,
                        "median_ms": median(thread_times) * 1000,
                        "max_ms": max(thread_times) * 1000,
                        "mean_ms": mean(thread_times) * 1000,
                        "total_ms": total_elapsed * 1000,
                    }
                )

        if results_by_combo:
            print("\nMulti-thread benchmark results:")
            for (pattern_text, op_name) in sorted(results_by_combo):
                result_rows = sorted(
                    results_by_combo[(pattern_text, op_name)],
                    key=lambda row: row["mean_ms"],
                )
                present_engines = {row["engine"] for row in result_rows}
                for engine_name, _, _ in self.engines:
                    if engine_name not in present_engines:
                        result_rows.append(
                            {
                                "engine": engine_name,
                                "threads": "n/a",
                                "min_ms": "n/a",
                                "median_ms": "n/a",
                                "max_ms": "n/a",
                                "mean_ms": "n/a",
                                "total_ms": "n/a",
                            }
                        )
                result_rows.sort(key=lambda row: row["mean_ms"] if isinstance(row["mean_ms"], (int, float)) else float("inf"))
                print(f"\nPattern: {pattern_text} | Operation: {op_name}")
                print(
                    tabulate(
                        result_rows,
                        headers="keys",
                        floatfmt=".3f",
                        tablefmt="github",
                    )
                )

    def _run_thread_benchmark(self, operation, subjects):
        start_barrier = threading.Barrier(THREAD_COUNT + 1)
        finish_barrier = threading.Barrier(THREAD_COUNT + 1)
        durations = [0.0] * THREAD_COUNT

        def worker(index: int):
            # Ensure threads are ready before timing
            start_barrier.wait()
            start_time = time.perf_counter()
            for _ in range(THREAD_ITERATIONS):
                for subject in subjects:
                    operation(subject)
            durations[index] = time.perf_counter() - start_time
            finish_barrier.wait()

        threads = [threading.Thread(target=worker, args=(idx,)) for idx in range(THREAD_COUNT)]
        for thread in threads:
            thread.start()

        start_barrier.wait()  # Wait for all workers to report ready
        global_start = time.perf_counter()
        finish_barrier.wait()  # Wait for all workers to finish work
        total_elapsed = time.perf_counter() - global_start

        for thread in threads:
            thread.join()

        self.assertGreaterEqual(total_elapsed, 0.0)
        return durations, total_elapsed


if __name__ == "__main__":
    unittest.main()
