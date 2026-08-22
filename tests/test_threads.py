# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Coverage tests for pcre.threads configuration helpers."""

from __future__ import annotations

import threading

import pytest

import pcre.threads as threads_mod


def test_threading_supported_false_on_low_core_count(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(threads_mod, "_cpu_total", lambda: 4)
    assert threads_mod.threading_supported() is False


def test_configure_threads_toggles_default(monkeypatch: pytest.MonkeyPatch) -> None:
    original = threads_mod.get_thread_default()
    try:
        assert threads_mod.configure_threads(enabled=True) is True
        assert threads_mod.get_thread_default() is True
        assert threads_mod.configure_threads(enabled=False) is False
        assert threads_mod.get_thread_default() is False
    finally:
        threads_mod.configure_threads(enabled=original)


def test_configure_threads_threshold_validation(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    original = threads_mod.get_auto_threshold()
    try:
        assert (
            threads_mod.configure_threads(threshold=1234)
            == threads_mod.get_thread_default()
        )
        assert threads_mod.get_auto_threshold() == 1234

        with pytest.raises(ValueError):
            threads_mod.configure_threads(threshold=-1)
        with pytest.raises(TypeError):
            threads_mod.configure_threads(threshold="not-an-int")
    finally:
        threads_mod.configure_threads(threshold=original)


def test_configure_thread_pool_clamps_workers(monkeypatch: pytest.MonkeyPatch) -> None:
    original_workers = getattr(threads_mod, "_THREAD_POOL_WORKERS", None)
    original_pool = getattr(threads_mod, "_THREAD_POOL", None)
    try:
        # Use a huge worker request; it should be clamped to the computed maximum.
        maximum = threads_mod._max_threads()
        workers = threads_mod.configure_thread_pool(max_workers=10000, preload=False)
        assert workers == maximum

        # Preload creates the pool eagerly.
        threads_mod.shutdown_thread_pool(wait=True)
        workers = threads_mod.configure_thread_pool(max_workers=2, preload=True)
        assert workers == max(1, min(2, maximum))
        assert threads_mod._THREAD_POOL is not None
    finally:
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod._THREAD_POOL = original_pool
        threads_mod._THREAD_POOL_WORKERS = original_workers


def test_configure_thread_pool_rejects_invalid_worker_count() -> None:
    with pytest.raises(TypeError):
        threads_mod.configure_thread_pool(max_workers="x")
    with pytest.raises(ValueError):
        threads_mod.configure_thread_pool(max_workers=0)


def test_get_thread_pool_size_initializes_once(monkeypatch: pytest.MonkeyPatch) -> None:
    original_workers = threads_mod._THREAD_POOL_WORKERS
    original_pool = threads_mod._THREAD_POOL
    try:
        threads_mod._THREAD_POOL_WORKERS = None
        threads_mod._THREAD_POOL = None
        size = threads_mod.get_thread_pool_size()
        assert size > 0
        # Second call returns the cached value.
        assert threads_mod.get_thread_pool_size() == size
    finally:
        threads_mod._THREAD_POOL_WORKERS = original_workers
        threads_mod._THREAD_POOL = original_pool


def test_shutdown_thread_pool_is_idempotent() -> None:
    threads_mod.shutdown_thread_pool(wait=True)
    threads_mod.shutdown_thread_pool(wait=False)


def test_max_threads_zero_when_threading_unsupported(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(threads_mod, "_cpu_total", lambda: 4)
    assert threads_mod._max_threads() == 0


def test_max_threads_prefers_performance_cluster(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(threads_mod, "_cpu_total", lambda: 16)
    monkeypatch.setattr(threads_mod, "_performance_cpu_total", lambda: 3)
    assert threads_mod._max_threads() == 3


def test_max_threads_falls_back_to_fraction_without_cluster_info(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(threads_mod, "_cpu_total", lambda: 16)
    monkeypatch.setattr(threads_mod, "_performance_cpu_total", lambda: 0)
    assert threads_mod._max_threads() == 4


def test_performance_cpu_count_falls_back_when_sysctl_is_unavailable(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    original = threads_mod._PERFORMANCE_CPU_TOTAL
    try:
        threads_mod._PERFORMANCE_CPU_TOTAL = None
        monkeypatch.setattr(threads_mod.sys, "platform", "darwin")
        monkeypatch.setattr(
            threads_mod.subprocess,
            "check_output",
            lambda *args, **kwargs: (_ for _ in ()).throw(OSError("sysctl")),
        )
        assert threads_mod._performance_cpu_total() == 0
        assert threads_mod._performance_cpu_total() == 0
    finally:
        threads_mod._PERFORMANCE_CPU_TOTAL = original


def test_performance_cpu_count_is_zero_outside_macos(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    original = threads_mod._PERFORMANCE_CPU_TOTAL
    try:
        threads_mod._PERFORMANCE_CPU_TOTAL = None
        monkeypatch.setattr(threads_mod.sys, "platform", "linux")
        assert threads_mod._performance_cpu_total() == 0
    finally:
        threads_mod._PERFORMANCE_CPU_TOTAL = original


def test_determine_worker_count_requires_supported_backend(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(threads_mod, "_max_threads", lambda: 0)
    with pytest.raises(RuntimeError, match="at least 8 CPU cores"):
        threads_mod._determine_worker_count(None)


def test_configure_thread_pool_shuts_down_existing_pool() -> None:
    original_pool = threads_mod._THREAD_POOL
    original_workers = threads_mod._THREAD_POOL_WORKERS
    try:
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod.configure_thread_pool(max_workers=1, preload=True)
        first_pool = threads_mod._THREAD_POOL
        assert first_pool is not None

        threads_mod.configure_thread_pool(max_workers=2, preload=True)
        assert threads_mod._THREAD_POOL is not first_pool
    finally:
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod._THREAD_POOL = original_pool
        threads_mod._THREAD_POOL_WORKERS = original_workers


def test_ensure_thread_pool_resizes_existing_pool() -> None:
    original_pool = threads_mod._THREAD_POOL
    original_workers = threads_mod._THREAD_POOL_WORKERS
    try:
        threads_mod.shutdown_thread_pool(wait=True)
        first_pool = threads_mod.ensure_thread_pool(max_workers=1)
        assert first_pool is not None

        second_pool = threads_mod.ensure_thread_pool(max_workers=2)
        assert second_pool is not first_pool
    finally:
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod._THREAD_POOL = original_pool
        threads_mod._THREAD_POOL_WORKERS = original_workers


def test_pool_submission_cannot_race_executor_replacement(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Reconfiguration waits until a leased executor accepts its batch."""

    original_pool = threads_mod._THREAD_POOL
    original_workers = threads_mod._THREAD_POOL_WORKERS
    entered_submit = threading.Event()
    release_submit = threading.Event()
    configured = threading.Event()
    original_submit = threads_mod.ThreadPoolExecutor.submit

    def gated_submit(executor, fn, *args, **kwargs):
        entered_submit.set()
        assert release_submit.wait(timeout=5)
        return original_submit(executor, fn, *args, **kwargs)

    monkeypatch.setattr(threads_mod.ThreadPoolExecutor, "submit", gated_submit)
    try:
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod.configure_thread_pool(max_workers=1, preload=True)

        submitted: list[object] = []

        def submitter() -> None:
            futures, _ = threads_mod.submit_thread_pool_tasks(
                [lambda: "accepted"], max_workers=1
            )
            submitted.extend(futures)

        def reconfigure() -> None:
            threads_mod.configure_thread_pool(max_workers=2, preload=False)
            configured.set()

        submit_thread = threading.Thread(target=submitter)
        submit_thread.start()
        assert entered_submit.wait(timeout=5)

        configure_thread = threading.Thread(target=reconfigure)
        configure_thread.start()
        assert not configured.wait(timeout=0.05)

        release_submit.set()
        submit_thread.join(timeout=5)
        configure_thread.join(timeout=5)
        assert not submit_thread.is_alive()
        assert not configure_thread.is_alive()
        assert configured.is_set()
        assert submitted[0].result(timeout=5) == "accepted"  # type: ignore[attr-defined]
    finally:
        release_submit.set()
        threads_mod.shutdown_thread_pool(wait=True)
        threads_mod._THREAD_POOL = original_pool
        threads_mod._THREAD_POOL_WORKERS = original_workers
