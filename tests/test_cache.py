# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import threading
from collections import OrderedDict
from typing import Any, Dict, List

import pytest

import pcre.cache as cache_mod


def _fresh_thread_cache() -> OrderedDict[Any, Any]:
    store: OrderedDict[Any, Any] = OrderedDict()
    cache_mod._THREAD_LOCAL.pattern_cache = store
    return store


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
