# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import json
import subprocess
import sys
import textwrap
import threading
from collections import OrderedDict
from typing import Any, Dict, List

import pytest

import pcre.cache as cache_mod


@pytest.fixture(autouse=True)
def _reset_cache_state() -> None:
    cache_mod.cache_strategy("thread-local")
    original_limit = cache_mod.get_cache_limit()
    cache_mod.clear_cache()
    try:
        yield
    finally:
        cache_mod.cache_strategy("thread-local")
        cache_mod.set_cache_limit(original_limit)
        cache_mod.clear_cache()


def _fresh_thread_cache() -> OrderedDict[Any, Any]:
    store: OrderedDict[Any, Any] = OrderedDict()
    cache_mod._THREAD_LOCAL.pattern_cache = store
    return store


def _run_cache_script(source: str) -> Dict[str, Any]:
    completed = subprocess.run(
        [sys.executable, "-c", source],
        check=True,
        capture_output=True,
        text=True,
    )
    if completed.stderr:
        raise AssertionError(f"unexpected stderr output: {completed.stderr}")
    return json.loads(completed.stdout)


def _benchmark_strategy(strategy: str, iterations: int = 5000) -> Dict[str, Any]:
    script = textwrap.dedent(
        f"""
        import json
        import time
        import pcre.cache as cache_mod

        def wrapper(compiled):
            return compiled

        cache_mod.cache_strategy({strategy!r})
        cache_mod.clear_cache()
        cache_mod.cached_compile("expr", 0, wrapper, jit=False)

        start = time.perf_counter()
        for _ in range({iterations}):
            cache_mod.cached_compile("expr", 0, wrapper, jit=False)
        elapsed = time.perf_counter() - start

        print(json.dumps({{"strategy": {strategy!r}, "iterations": {iterations}, "elapsed": elapsed}}))
        """
    )
    return _run_cache_script(script)


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

    cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    with pytest.raises(RuntimeError):
        cache_mod.cache_strategy("global")


def test_cache_strategy_global_shares_cache_across_threads() -> None:
    script = textwrap.dedent(
        """
        import json
        import threading
        import pcre.cache as cache_mod

        cache_mod.cache_strategy("global")
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

    result = _run_cache_script(script)
    assert result["calls"] == 1
    assert result["main_result"] == result["worker_result"] == "compiled:1"


def test_cache_strategy_benchmark() -> None:
    thread_stats = _benchmark_strategy("thread-local", iterations=2000)
    global_stats = _benchmark_strategy("global", iterations=2000)

    assert thread_stats["strategy"] == "thread-local"
    assert global_stats["strategy"] == "global"
    assert thread_stats["elapsed"] >= 0.0
    assert global_stats["elapsed"] >= 0.0
