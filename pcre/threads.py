# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Shared thread-pool management for the high-level PCRE helpers."""

from __future__ import annotations

import atexit
import os
import sys
import threading
from concurrent.futures import ThreadPoolExecutor
from typing import Final


_POOL_NAME: Final[str] = "pcre-worker"
_MIN_CORES_FOR_THREADS: Final[int] = 8
_THREAD_POOL_LOCK = threading.RLock()
_THREAD_POOL: ThreadPoolExecutor | None = None
_THREAD_POOL_WORKERS: int | None = None
_THREAD_AUTO_THRESHOLD: int = 60_000


def _cpu_total() -> int:
    return os.cpu_count() or 1


def threading_supported() -> bool:
    """Report whether the threaded backend is allowed on this machine."""

    return _cpu_total() >= _MIN_CORES_FOR_THREADS


_THREADS_DEFAULT: bool = threading_supported() and not (
    hasattr(sys, "_is_gil_enabled") and sys._is_gil_enabled()
)


def _max_threads() -> int:
    if not threading_supported():
        return 0
    cpu_total = _cpu_total()
    return max(1, cpu_total // 4)


def _determine_worker_count(value: int | None) -> int:
    maximum = _max_threads()
    if maximum == 0:
        raise RuntimeError(
            "Threaded backend requires at least 8 CPU cores; refusing to create pool"
        )
    if value is None:
        return maximum

    try:
        resolved = int(value)
    except (TypeError, ValueError) as exc:  # pragma: no cover - defensive conversion
        raise TypeError("max_workers must be an int or None") from exc

    if resolved <= 0:
        raise ValueError("max_workers must be >= 1")

    if resolved > maximum:
        resolved = maximum
    return resolved


def ensure_thread_pool(max_workers: int | None = None) -> ThreadPoolExecutor:
    """Return the shared executor, creating or resizing it if required."""

    global _THREAD_POOL
    global _THREAD_POOL_WORKERS

    target = _determine_worker_count(
        max_workers if max_workers is not None else _THREAD_POOL_WORKERS
    )

    with _THREAD_POOL_LOCK:
        if _THREAD_POOL is not None and _THREAD_POOL_WORKERS == target:
            return _THREAD_POOL

        if _THREAD_POOL is not None:
            _THREAD_POOL.shutdown(wait=True)

        _THREAD_POOL = ThreadPoolExecutor(
            max_workers=target,
            thread_name_prefix=_POOL_NAME,
        )
        _THREAD_POOL_WORKERS = target
        return _THREAD_POOL


def configure_thread_pool(*, max_workers: int | None = None, preload: bool = False) -> int:
    """Set the shared executor size used by :func:`parallel_map`.

    Returns the effective worker count after applying the update.
    """

    global _THREAD_POOL
    global _THREAD_POOL_WORKERS

    workers = _determine_worker_count(max_workers)

    with _THREAD_POOL_LOCK:
        _THREAD_POOL_WORKERS = workers
        if _THREAD_POOL is not None:
            _THREAD_POOL.shutdown(wait=True)
            _THREAD_POOL = None

    if preload:
        ensure_thread_pool(workers)

    return workers


def shutdown_thread_pool(*, wait: bool = True) -> None:
    """Dispose of the shared thread pool if it has been created."""

    global _THREAD_POOL

    with _THREAD_POOL_LOCK:
        pool = _THREAD_POOL
        _THREAD_POOL = None

    if pool is not None:
        pool.shutdown(wait=wait)


def get_thread_pool_size() -> int:
    """Return the current configured worker count (creating defaults if needed)."""

    global _THREAD_POOL_WORKERS
    if _THREAD_POOL_WORKERS is None:
        _THREAD_POOL_WORKERS = _determine_worker_count(None)
    return _THREAD_POOL_WORKERS


def configure_threads(*, enabled: bool | None = None, threshold: int | None = None) -> bool:
    """Adjust the global threading defaults and/or auto threshold."""

    global _THREADS_DEFAULT
    global _THREAD_AUTO_THRESHOLD

    if enabled is not None:
        _THREADS_DEFAULT = bool(enabled)

    if threshold is not None:
        try:
            new_threshold = int(threshold)
        except (TypeError, ValueError) as exc:  # pragma: no cover - defensive
            raise TypeError("threshold must be an int") from exc
        if new_threshold < 0:
            raise ValueError("threshold must be >= 0")
        _THREAD_AUTO_THRESHOLD = new_threshold

    return _THREADS_DEFAULT


def get_thread_default() -> bool:
    return _THREADS_DEFAULT


def get_auto_threshold() -> int:
    return _THREAD_AUTO_THRESHOLD


atexit.register(shutdown_thread_pool)


__all__ = [
    "configure_thread_pool",
    "ensure_thread_pool",
    "shutdown_thread_pool",
    "get_thread_pool_size",
    "configure_threads",
    "get_thread_default",
    "get_auto_threshold",
    "threading_supported",
]
