# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import json
import os
import subprocess
import sys
import textwrap
import threading
from collections import OrderedDict
from pathlib import Path
from typing import Any, Dict, List

import pcre
import pcre.cache as cache_mod
import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[1]


@pytest.fixture(autouse=True)
def _reset_cache_state() -> None:
    if cache_mod.cache_strategy() != "thread-local":
        pytest.skip("global pattern cache enabled via environment; thread-local tests skipped")
    original_limit = cache_mod.get_cache_limit()
    cache_mod.clear_cache()
    try:
        yield
    finally:
        cache_mod.set_cache_limit(original_limit)
        cache_mod.clear_cache()


def _fresh_thread_cache() -> OrderedDict[Any, Any]:
    store: OrderedDict[Any, Any] = OrderedDict()
    cache_mod._THREAD_LOCAL.pattern_cache = store
    return store


def _run_cache_script(source: str, env_overrides: Dict[str, str] | None = None) -> Dict[str, Any]:
    env = os.environ.copy()
    pythonpath_entries = [str(PROJECT_ROOT)]
    existing_pythonpath = env.get("PYTHONPATH")
    if existing_pythonpath:
        pythonpath_entries.append(existing_pythonpath)
    env["PYTHONPATH"] = os.pathsep.join(pythonpath_entries)
    if env_overrides:
        env.update(env_overrides)

    completed = subprocess.run(
        [sys.executable, "-c", source],
        check=True,
        capture_output=True,
        text=True,
        env=env,
    )
    if completed.stderr:
        raise AssertionError(f"unexpected stderr output: {completed.stderr}")
    return json.loads(completed.stdout)


def _format_table(headers: List[str], rows: List[List[str]]) -> List[str]:
    display_rows = [headers] + rows
    widths = [max(len(row[idx]) for row in display_rows) for idx in range(len(headers))]

    def _format(row: List[str]) -> str:
        return " | ".join(cell.ljust(widths[idx]) for idx, cell in enumerate(row))

    separator = "-+-".join("-" * width for width in widths)
    lines = [_format(headers), separator]
    lines.extend(_format(row) for row in rows)
    return lines


def _emit_table(pytestconfig: pytest.Config, title: str, headers: List[str], rows: List[List[str]]) -> None:
    lines = _format_table(headers, rows)
    reporter = pytestconfig.pluginmanager.get_plugin("terminalreporter")
    if reporter is None:  # pragma: no cover - fallback for unusual runners
        print(f"\n{title}")
        for line in lines:
            print(line)
        return

    writer = getattr(reporter, "_tw", None)
    original_hasmarkup = None
    if writer is not None:
        original_hasmarkup = writer.hasmarkup
        writer.hasmarkup = False

    try:
        reporter.write_sep("-", title)
        for line in lines:
            reporter.write_line(line)
    finally:
        if writer is not None and original_hasmarkup is not None:
            writer.hasmarkup = original_hasmarkup


def _benchmark_strategy(strategy: str, iterations: int = 20000, threads: int = 1) -> Dict[str, Any]:
    script = textwrap.dedent(
        f"""
        import json
        import threading
        import time
        import pcre.cache as cache_mod

        def wrapper(compiled):
            return compiled

        STRATEGY = {strategy!r}
        ITERATIONS = {iterations}
        THREAD_COUNT = {threads}
        REPEATS = 5

        if cache_mod.cache_strategy() != STRATEGY:
            raise SystemExit(f"strategy mismatch: {strategy!r} != {cache_mod.cache_strategy()!r}")
        cache_mod.clear_cache()
        cache_mod.cached_compile("expr", 0, wrapper, jit=False)

        def run_single():
            best = float("inf")
            for _ in range(REPEATS):
                start = time.perf_counter()
                for _ in range(ITERATIONS):
                    cache_mod.cached_compile("expr", 0, wrapper, jit=False)
                elapsed = time.perf_counter() - start
                if elapsed < best:
                    best = elapsed
            return best

        def run_threaded():
            best = float("inf")
            for _ in range(REPEATS):
                errors = []
                start_barrier = threading.Barrier(THREAD_COUNT + 1)
                stop_barrier = threading.Barrier(THREAD_COUNT + 1)

                def worker() -> None:
                    try:
                        cache_mod.cached_compile("expr", 0, wrapper, jit=False)
                        start_barrier.wait()
                        for _ in range(ITERATIONS):
                            cache_mod.cached_compile("expr", 0, wrapper, jit=False)
                        stop_barrier.wait()
                    except BaseException as exc:  # pragma: no cover - surfaced in main thread
                        errors.append(repr(exc))
                        raise

                workers = [threading.Thread(target=worker, name=f"bench-worker-{{idx}}") for idx in range(THREAD_COUNT)]
                for thread in workers:
                    thread.start()

                start_barrier.wait()
                start = time.perf_counter()
                stop_barrier.wait()
                elapsed = time.perf_counter() - start

                for thread in workers:
                    thread.join()

                if errors:
                    raise RuntimeError("worker thread failed", errors)

                if elapsed < best:
                    best = elapsed

            return best

        if THREAD_COUNT == 1:
            elapsed = run_single()
        else:
            elapsed = run_threaded()

        print(
            json.dumps(
                dict(
                    strategy=STRATEGY,
                    iterations=ITERATIONS,
                    threads=THREAD_COUNT,
                    elapsed=elapsed,
                    total_calls=ITERATIONS * THREAD_COUNT,
                )
            )
        )
        """
    )
    env_overrides = {
        "PYPCRE_CACHE_PATTERN_GLOBAL": "1" if strategy == "global" else "0"
    }
    return _run_cache_script(script, env_overrides=env_overrides)


def test_cached_compile_thread_local_isolation(monkeypatch: pytest.MonkeyPatch) -> None:
    compile_calls: List[str] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> str:
        compile_calls.append(f"{threading.current_thread().name}:{pattern}:{flags}:{jit}")
        return f"compiled:{len(compile_calls)}"

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    main_store = _fresh_thread_cache()

    def wrapper(raw: str) -> str:
        return f"wrapped:{raw}"

    main_result = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    assert main_result == "wrapped:compiled:1"
    assert list(main_store.keys()) == [("expr", 0, False)]

    barrier = threading.Barrier(2)
    worker_store_keys: List[Any] = []
    worker_results: List[str] = []
    worker_errors: List[BaseException] = []

    def worker() -> None:
        try:
            _fresh_thread_cache()
            barrier.wait(timeout=5)
            result = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
            worker_results.append(result)
            worker_store_keys.extend(cache_mod._THREAD_LOCAL.pattern_cache.keys())
        except BaseException as exc:  # pragma: no cover - surfaced in main thread
            worker_errors.append(exc)
            raise

    thread = threading.Thread(target=worker, name="cache-worker")
    thread.start()

    try:
        barrier.wait(timeout=5)
    finally:
        thread.join(timeout=5)

    if thread.is_alive():
        raise AssertionError("worker thread did not finish")

    assert not worker_errors
    assert worker_results == ["wrapped:compiled:2"]
    assert worker_store_keys == [("expr", 0, False)]
    assert compile_calls == [
        "MainThread:expr:0:False",
        "cache-worker:expr:0:False",
    ]
    assert list(main_store.keys()) == [("expr", 0, False)]  # untouched by worker


def test_clear_cache_only_resets_current_thread(monkeypatch: pytest.MonkeyPatch) -> None:
    compile_calls: List[str] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> str:
        compile_calls.append(f"{threading.current_thread().name}:{pattern}:{flags}:{jit}")
        return f"compiled:{len(compile_calls)}"

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    main_store = _fresh_thread_cache()

    def wrapper(raw: str) -> str:
        return raw

    cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    assert len(main_store) == 1

    worker_ready = threading.Event()
    resume_event = threading.Event()
    worker_state: Dict[str, Any] = {}
    worker_errors: List[BaseException] = []

    def worker() -> None:
        try:
            _fresh_thread_cache()
            first = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
            worker_state["first_result"] = first
            worker_state["initial_len"] = len(cache_mod._THREAD_LOCAL.pattern_cache)
            worker_ready.set()
            if not resume_event.wait(timeout=5):
                raise AssertionError("resume signal not received")
            second = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
            worker_state["second_result"] = second
            worker_state["final_len"] = len(cache_mod._THREAD_LOCAL.pattern_cache)
        except BaseException as exc:  # pragma: no cover - surfaced in main thread
            worker_errors.append(exc)
            raise

    thread = threading.Thread(target=worker, name="cache-worker")
    thread.start()

    try:
        if not worker_ready.wait(timeout=5):
            raise AssertionError("worker failed to signal readiness")
        assert len(main_store) == 1
        cache_mod.clear_cache()
        assert len(main_store) == 0
        resume_event.set()
    finally:
        thread.join(timeout=5)

    if thread.is_alive():
        raise AssertionError("worker thread did not finish")

    assert not worker_errors
    assert worker_state["initial_len"] == 1
    assert worker_state["final_len"] == 1
    assert worker_state["first_result"] == worker_state["second_result"] == "compiled:2"
    assert compile_calls == [
        "MainThread:expr:0:False",
        "cache-worker:expr:0:False",
    ]


def test_cache_limit_thread_local_isolated() -> None:
    main_original_limit = cache_mod.get_cache_limit()
    main_store = _fresh_thread_cache()
    cache_mod.set_cache_limit(7)

    worker_before: List[int | None] = []
    worker_after: List[int | None] = []
    worker_done = threading.Event()

    def worker() -> None:
        _fresh_thread_cache()
        worker_before.append(cache_mod.get_cache_limit())
        cache_mod.set_cache_limit(2)
        worker_after.append(cache_mod.get_cache_limit())
        worker_done.set()

    thread = threading.Thread(target=worker, name="cache-limit-worker")
    thread.start()

    try:
        if not worker_done.wait(timeout=5):
            raise AssertionError("worker thread did not finish")
    finally:
        thread.join(timeout=5)

    if thread.is_alive():
        raise AssertionError("worker thread did not finish")

    current_limit = cache_mod.get_cache_limit()
    try:
        assert current_limit == 7
    finally:
        cache_mod.set_cache_limit(main_original_limit)

    assert worker_before == [main_original_limit]
    assert worker_after == [2]
    assert cache_mod.get_cache_limit() == main_original_limit
    assert list(main_store.keys()) == []


def test_cache_strategy_query_returns_active() -> None:
    assert cache_mod.cache_strategy() == "thread-local"
    assert cache_mod.cache_strategy("thread-local") == "thread-local"


def test_cache_strategy_invalid_value() -> None:
    with pytest.raises(ValueError):
        cache_mod.cache_strategy("totally-invalid")


def test_cache_strategy_cannot_switch_after_use(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(cache_mod._pcre2, "compile", lambda pattern, *, flags=0, jit=False: pattern)

    def wrapper(raw: Any) -> Any:
        return raw

    with pytest.raises(RuntimeError):
        cache_mod.cache_strategy("global")

    cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    with pytest.raises(RuntimeError):
        cache_mod.cache_strategy("global")


def test_thread_cache_cleanup_no_leak() -> None:
    script = textwrap.dedent(
        """
        import gc
        import json
        import threading
        import pcre_ext_c as ext

        def worker() -> None:
            for _ in range(200):
                ext.match("foo", "foo")

        threads = [threading.Thread(target=worker) for _ in range(10)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        gc.collect()
        print(json.dumps({"count": ext._debug_thread_cache_count()}))
        """
    )
    data = _run_cache_script(script, env_overrides={"PYPCRE_DEBUG": "1"})
    assert data["count"] == 0


def test_shared_pattern_concurrent_execution_produces_expected_results() -> None:
    pcre.clear_cache()

    pattern = pcre.compile(r"(?P<num>\d+)-(?P<word>[A-Za-z]+)")

    subject_one = "123-alpha tail"
    subject_two = "prefix 456-beta suffix"
    subject_three = "outer 789-gamma end"
    subject_four = "zzz-no-hit"

    cases: List[tuple[str, int, int, tuple[str, str] | None]] = [
        (subject_one, 0, subject_one.index(" "), ("123", "alpha")),
        (subject_two, subject_two.index("456"), subject_two.index(" suffix"), ("456", "beta")),
        (subject_three, subject_three.index("789"), subject_three.index(" end"), ("789", "gamma")),
        (subject_four, 0, len(subject_four), None),
    ]

    thread_count = 4
    iterations = 50
    start_barrier = threading.Barrier(thread_count + 1)

    errors: List[BaseException] = []
    results: List[List[tuple[str, str] | None]] = [[] for _ in range(thread_count)]

    def worker(index: int) -> None:
        try:
            start_barrier.wait(timeout=5)
            local: List[tuple[str, str] | None] = []
            for _ in range(iterations):
                for subject, pos, endpos, expected in cases:
                    match = pattern.search(subject, pos=pos, endpos=endpos)
                    if expected is None:
                        if match is not None:
                            raise AssertionError(f"expected no match for {subject!r}, got {match.group(0)!r}")
                        local.append(None)
                        continue
                    if match is None:
                        raise AssertionError(f"missing match for {subject!r}")
                    local.append((match.group("num"), match.group("word")))
            results[index] = local
        except BaseException as exc:  # pragma: no cover - surfaced in main thread
            errors.append(exc)
            raise

    threads = [
        threading.Thread(target=worker, args=(idx,), name=f"pattern-worker-{idx}")
        for idx in range(thread_count)
    ]

    for thread in threads:
        thread.start()

    barrier_failed = False
    try:
        start_barrier.wait(timeout=5)
    except threading.BrokenBarrierError:
        barrier_failed = True

    for thread in threads:
        thread.join(timeout=10)

    if any(thread.is_alive() for thread in threads):
        pytest.fail("worker thread did not finish")

    if errors:
        raise errors[0]

    if barrier_failed:
        pytest.fail("worker threads failed to synchronize before execution")

    expected_sequence: List[tuple[str, str] | None] = []
    for _ in range(iterations):
        expected_sequence.extend(case[3] for case in cases)

    for local in results:
        assert local == expected_sequence


def test_cache_strategy_global_shares_cache_across_threads() -> None:
    script = textwrap.dedent(
        """
        import json
        import threading
        import pcre.cache as cache_mod

        if cache_mod.cache_strategy() != "global":
            raise SystemExit("global cache not active")
        cache_mod.clear_cache()

        compile_calls = []

        def fake_compile(pattern, *, flags=0, jit=False):
            compile_calls.append((pattern, flags, bool(jit)))
            return f"compiled:{len(compile_calls)}"

        cache_mod._pcre2.compile = fake_compile

        def wrapper(raw):
            return raw

        main_result = cache_mod.cached_compile("expr", 0, wrapper, jit=False)

        worker_results = []

        def worker():
            worker_results.append(cache_mod.cached_compile("expr", 0, wrapper, jit=False))

        thread = threading.Thread(target=worker)
        thread.start()
        thread.join()

        if not worker_results:
            raise RuntimeError("worker failed to produce a result")

        print(
            json.dumps(
                {
                    "calls": len(compile_calls),
                    "main_result": main_result,
                    "worker_result": worker_results[0],
                }
            )
        )
        """
    )

    result = _run_cache_script(script, env_overrides={"PYPCRE_CACHE_PATTERN_GLOBAL": "1"})
    assert result["calls"] == 1
    assert result["main_result"] == result["worker_result"] == "compiled:1"


def test_cache_strategy_benchmark(pytestconfig: pytest.Config) -> None:
    iterations = 20_000
    cpu_count = os.cpu_count() or 1
    desired_thread_counts = [1, 2, 4, 8, 16, 32]
    max_threads = max(1, cpu_count // 2)
    # Limit concurrency to half of the visible cores to avoid full-saturation regressions while still
    # covering progressively larger thread counts when possible.
    thread_counts = [count for count in desired_thread_counts if count == 1 or count <= max_threads]
    scenarios = [
        (strategy, thread_count)
        for thread_count in thread_counts
        for strategy in ("thread-local", "global")
    ]

    results: Dict[tuple[str, int], Dict[str, Any]] = {}
    for strategy, thread_count in scenarios:
        stats = _benchmark_strategy(strategy, iterations=iterations, threads=thread_count)
        key = (strategy, thread_count)
        results[key] = stats
        assert stats["strategy"] == strategy
        assert stats["threads"] == thread_count
        assert stats["elapsed"] >= 0.0

    headers = ["strategy", "threads", "total calls", "total ms", "per call ns", "relative"]
    for thread_count in thread_counts:
        local_stats = results[("thread-local", thread_count)]
        global_stats = results[("global", thread_count)]
        # Allow a small tolerance for measurement noise but enforce thread-local is not slower for
        # thread counts within the practical coverage window (up to half the cores or 8 threads).
        if thread_count <= min(max_threads, 8):
            assert local_stats["elapsed"] <= global_stats["elapsed"] * 1.10

        rows: List[List[str]] = []
        baseline = local_stats["elapsed"] or 1.0
        for strategy in ("thread-local", "global"):
            stats = results[(strategy, thread_count)]
            total_calls = stats.get("total_calls", stats["iterations"] * stats.get("threads", 1))
            per_call_ns = (stats["elapsed"] / total_calls) * 1e9 if total_calls else float("nan")
            relative = stats["elapsed"] / baseline if baseline else float("nan")
            rows.append(
                [
                    stats["strategy"],
                    str(stats["threads"]),
                    str(total_calls),
                    f"{stats['elapsed'] * 1000:.3f}",
                    f"{per_call_ns:.1f}",
                    f"{relative:.2f}x",
                ]
            )

        title = "Cache strategy benchmark (single-thread)" if thread_count == 1 else f"Cache strategy benchmark (threads={thread_count})"
        _emit_table(pytestconfig, title, headers, rows)
