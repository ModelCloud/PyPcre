# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Coverage tests for pcre.threads configuration helpers."""

from __future__ import annotations

import pcre.threads as threads_mod
import pytest


def test_threading_supported_false_on_low_core_count(monkeypatch: pytest.MonkeyPatch) -> None:
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


def test_configure_threads_threshold_validation(monkeypatch: pytest.MonkeyPatch) -> None:
    original = threads_mod.get_auto_threshold()
    try:
        assert threads_mod.configure_threads(threshold=1234) == threads_mod.get_thread_default()
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


def test_max_threads_zero_when_threading_unsupported(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(threads_mod, "_cpu_total", lambda: 4)
    assert threads_mod._max_threads() == 0


def test_determine_worker_count_requires_supported_backend(monkeypatch: pytest.MonkeyPatch) -> None:
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
