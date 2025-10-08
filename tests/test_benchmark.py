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


RUN_BENCHMARKS = os.getenv("PCRE2_RUN_BENCHMARKS") == "1"
THREAD_COUNT = int(os.getenv("PCRE2_BENCH_THREADS", "16"))
SINGLE_ITERATIONS = int(os.getenv("PCRE2_BENCH_ITERS", "200"))
THREAD_ITERATIONS = int(os.getenv("PCRE2_BENCH_THREAD_ITERS", "40"))


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


@unittest.skipUnless(RUN_BENCHMARKS, "Set PCRE2_RUN_BENCHMARKS=1 to enable benchmark tests")
class TestRegexBenchmarks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.engines = [
            ("re", re, lambda pattern: re.compile(pattern)),
            ("pcre", pcre, lambda pattern: pcre.compile(pattern)),
        ]
        if external_pcre2 is not None:
            cls.engines.append(("pcre2", external_pcre2, lambda pattern: external_pcre2.compile(pattern)))
        if external_regex is not None:
            cls.engines.append(("regex", external_regex, lambda pattern: external_regex.compile(pattern)))
        if SINGLE_ITERATIONS <= 0 or THREAD_ITERATIONS <= 0:
            raise unittest.SkipTest("Iterations must be positive for meaningful benchmarks")

    def test_single_thread_patterns(self):
        results = []
        for engine_name, module, compile_fn in self.engines:
            module_ops = _build_module_operations(module)
            for pattern_text, subjects in PATTERN_CASES:
                compiled = compile_fn(pattern_text)
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
                        results.append(
                            {
                                "engine": engine_name,
                                "pattern": pattern_text,
                                "operation": op_name,
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
                        results.append(
                            {
                                "engine": engine_name,
                                "pattern": pattern_text,
                                "operation": op_name,
                                "calls": expected_calls,
                                "total_ms": elapsed * 1000,
                                "per_call_ns": (elapsed / expected_calls) * 1e9,
                            }
                        )

        if results:
            print("\nSingle-thread benchmark results:")
            print(
                tabulate(
                    results,
                    headers="keys",
                    floatfmt=".3f",
                    tablefmt="github",
                )
            )

    def test_multi_threaded_match(self):
        pattern_text, subjects = PATTERN_CASES[0]
        results = []
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
                results.append(
                    {
                        "engine": engine_name,
                        "operation": op_name,
                        "threads": THREAD_COUNT,
                        "min_ms": min(thread_times) * 1000,
                        "median_ms": median(thread_times) * 1000,
                        "max_ms": max(thread_times) * 1000,
                        "mean_ms": mean(thread_times) * 1000,
                        "total_ms": total_elapsed * 1000,
                    }
                )

        if results:
            print("\nMulti-thread benchmark results:")
            print(
                tabulate(
                    results,
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
