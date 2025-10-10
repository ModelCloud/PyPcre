# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import os
import threading
import time
import unittest
from unittest import mock

import pcre
from pcre import Flag
from pcre import threads as thread_utils


class TestThreadedBackend(unittest.TestCase):
    def setUp(self):
        pcre.clear_cache()
        pcre.configure_threads(enabled=True, threshold=60_000)

    def _skip_if_thread_barred(self) -> None:
        if not thread_utils.threading_supported():
            self.skipTest("threaded backend requires >=8 CPU cores")

    def test_parallel_map_disabled_via_flag(self):
        pattern = pcre.compile(r"requires-flag", Flag.NO_THREADS)
        subjects = ["alpha", "beta"]
        with self.assertRaises(RuntimeError):
            pcre.parallel_map(pattern, subjects)

    def test_parallel_map_auto_default_runs_sequential_below_threshold(self):
        pattern = pcre.compile(r"seq")
        subjects = ["a" * 1000 for _ in range(3)]

        with mock.patch("pcre.pcre.ensure_thread_pool") as ensure_mock:
            results = pcre.parallel_map(pattern, subjects)

        ensure_mock.assert_not_called()
        self.assertEqual(results, [None, None, None])
        self.assertEqual(pattern.thread_mode, "auto")

    def test_parallel_map_auto_default_threads_above_threshold(self):
        self._skip_if_thread_barred()
        pattern = pcre.compile(r"seq")
        subjects = ["a" * 70_000, "b" * 70_000]

        with mock.patch("pcre.pcre.ensure_thread_pool") as ensure_mock:
            ensure_mock.side_effect = lambda *args, **kwargs: thread_utils.ensure_thread_pool(*args, **kwargs)
            results = pcre.parallel_map(pattern, subjects)

        self.assertTrue(ensure_mock.called)
        self.assertEqual(results, [None, None])
        self.assertEqual(pattern.thread_mode, "auto")

    def test_parallel_map_auto_rejects_non_text_subjects(self):
        pattern = pcre.compile(r"seq")
        with self.assertRaises(TypeError):
            pcre.parallel_map(pattern, [12345])

    def test_parallel_map_with_flag(self):
        subjects = ["alpha", "beta", "gamma"]
        pattern = pcre.compile(r"\w+", Flag.THREADS)
        results = pcre.parallel_map(pattern, subjects, method="search")
        self.assertEqual([match.group(0) for match in results], subjects)
        self.assertTrue(pattern.use_threads)

    def test_pattern_parallel_map(self):
        pattern = pcre.compile(r"\d+", Flag.THREADS)
        subjects = ["1", "22", "nope"]
        results = pattern.parallel_map(subjects, method="findall")
        self.assertEqual(results, [["1"], ["22"], []])

    def test_compile_existing_pattern_toggle(self):
        pattern = pcre.compile(r"foo")
        self.assertFalse(pattern.use_threads)
        self.assertEqual(pattern.thread_mode, "auto")
        toggled = pcre.compile(pattern, Flag.THREADS)
        self.assertIs(toggled, pattern)
        self.assertTrue(pattern.use_threads)

    def test_parallel_map_flags_argument(self):
        subjects = ["abc", "xyz"]
        results = pcre.parallel_map(r"\w+", subjects, flags=Flag.THREADS)
        self.assertEqual([match.group(0) for match in results], subjects)

    def test_parallel_map_unsupported_method(self):
        pattern = pcre.compile(r"foo", Flag.THREADS)
        with self.assertRaises(ValueError):
            pcre.parallel_map(pattern, ["foo"], method="sub")

    def test_benchmark_non_threaded_baseline(self):
        subjects = ["bench"] * 32
        sequential_pattern = pcre.compile(r"bench")
        start_seq = time.perf_counter()
        sequential_results = [sequential_pattern.search(subject) for subject in subjects]
        seq_elapsed = time.perf_counter() - start_seq

        threaded_pattern = pcre.compile(r"bench", Flag.THREADS)
        start_par = time.perf_counter()
        threaded_results = pcre.parallel_map(
            threaded_pattern,
            subjects,
            method="search",
        )
        par_elapsed = time.perf_counter() - start_par

        self.assertEqual(
            [bool(result) for result in sequential_results],
            [bool(result) for result in threaded_results],
        )
        self.assertGreaterEqual(seq_elapsed, 0.0)
        self.assertGreaterEqual(par_elapsed, 0.0)

    def test_parallel_cross_over_lengths(self):
        self._skip_if_thread_barred()
        base_delay = 1e-05
        thread_penalty = 0.005
        tolerance = 0.003
        base_lengths = [1024, 4096, 16384]
        incremental_lengths = list(range(16 * 1024, 1024 * 1024 + 1, 16 * 1024))
        lengths = sorted(set(base_lengths + incremental_lengths))
        subject_count = 2
        pattern_cls = type(pcre.compile(r"baseline"))

        for method in ("match", "search", "fullmatch", "findall"):
            original = getattr(pattern_cls, method)

            def instrumented(self, subject, *, pos=0, endpos=None, options=0):
                chunks = max(1, len(subject) // 1024)
                delay = chunks * base_delay
                if threading.current_thread().name.startswith("pcre-worker"):
                    time.sleep(delay + thread_penalty)
                else:
                    time.sleep(delay)
                return original(self, subject, pos=pos, endpos=endpos, options=options)

            with mock.patch.object(pattern_cls, method, instrumented):
                pcre.clear_cache()
                pattern = pcre.compile(r"\w+", Flag.THREADS)

                slower_lengths: list[int] = []
                faster_lengths: list[int] = []
                parity_lengths: list[int] = []
                timings: list[tuple[int, float, float]] = []

                for length in lengths:
                    subjects = ["x" * length for _ in range(subject_count)]

                    start_seq = time.perf_counter()
                    sequential_results = [
                        getattr(pattern, method)(subject) for subject in subjects
                    ]
                    seq_elapsed = time.perf_counter() - start_seq

                    start_par = time.perf_counter()
                    parallel_results = pattern.parallel_map(subjects, method=method)
                    par_elapsed = time.perf_counter() - start_par

                    timings.append((length, seq_elapsed, par_elapsed))

                    if method == "findall":
                        self.assertEqual(sequential_results, parallel_results)
                    else:
                        sequential_groups = [
                            result.group(0) if result is not None else None
                            for result in sequential_results
                        ]
                        parallel_groups = [
                            result.group(0) if result is not None else None
                            for result in parallel_results
                        ]
                        self.assertEqual(sequential_groups, parallel_groups)

                    if par_elapsed + tolerance < seq_elapsed:
                        faster_lengths.append(length)
                    elif seq_elapsed + tolerance < par_elapsed:
                        slower_lengths.append(length)
                    else:
                        parity_lengths.append(length)

                self.assertTrue(
                    slower_lengths,
                    msg=f"{method} had no lengths where parallel execution was slower",
                )
                if not faster_lengths:
                    cpu_cores = os.cpu_count() or 0
                    print(
                        f"threaded backend warning: {method} saw no faster lengths; "
                        f"detected {cpu_cores} CPU cores"
                    )

                self.assertTrue(
                    faster_lengths,
                    msg=f"{method} had no lengths where parallel execution was faster",
                )

                crossover = min(faster_lengths)
                slower_cap = max(slower_lengths)

                print(
                    f"thread crossover {method}: slower ≤ {slower_cap} bytes, "
                    f"faster ≥ {crossover} bytes, parity={parity_lengths}"
                )
                for length, seq_elapsed, par_elapsed in sorted(timings):
                    delta = seq_elapsed - par_elapsed
                    ratio = (seq_elapsed / par_elapsed) if par_elapsed else float("inf")
                    if par_elapsed + tolerance < seq_elapsed:
                        trend = "threaded faster"
                    elif seq_elapsed + tolerance < par_elapsed:
                        trend = "threaded slower"
                    else:
                        trend = "parity"
                    print(
                        f"  {method:8} len={length:7}B seq={seq_elapsed:.4f}s par={par_elapsed:.4f}s "
                        f"Δ={delta:+.4f}s ratio={ratio:.2f}x -> {trend}"
                    )

                self.assertLess(
                    min(slower_lengths),
                    min(faster_lengths),
                    msg=(
                        f"Expected a crossover for {method}, but slower lengths {slower_lengths} "
                        f"do not precede faster lengths {faster_lengths}"
                    ),
                )


if __name__ == "__main__":
    unittest.main()
