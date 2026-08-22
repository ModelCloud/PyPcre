# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Shared thread-pool management for the high-level PCRE helpers."""

from __future__ import annotations

import atexit
import os
import subprocess
import sys
import threading
from collections.abc import Callable, Iterator
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
from typing import Final

_POOL_NAME: Final[str] = "pcre-worker"
_MIN_CORES_FOR_THREADS: Final[int] = 8
_THREAD_POOL_LOCK = threading.RLock()
_THREAD_POOL: ThreadPoolExecutor | None = None
_THREAD_POOL_WORKERS: int | None = None
_THREAD_AUTO_THRESHOLD: int = 60_000
_PERFORMANCE_CPU_TOTAL: int | None = None


def _cpu_total() -> int:
    return os.cpu_count() or 1


def threading_supported() -> bool:
    """Report whether the threaded backend is allowed on this machine."""

    return _cpu_total() >= _MIN_CORES_FOR_THREADS


def _performance_cpu_total() -> int:
    """Return macOS performance-tier logical CPUs when the kernel exposes it."""

    global _PERFORMANCE_CPU_TOTAL
    with _THREAD_POOL_LOCK:
        if _PERFORMANCE_CPU_TOTAL is not None:
            return _PERFORMANCE_CPU_TOTAL
        if sys.platform != "darwin":
            _PERFORMANCE_CPU_TOTAL = 0
            return 0
        try:
            value = subprocess.check_output(
                ["sysctl", "-n", "hw.perflevel0.logicalcpu"],
                text=True,
                stderr=subprocess.DEVNULL,
            )
            _PERFORMANCE_CPU_TOTAL = max(0, int(value.strip()))
        except (OSError, ValueError, subprocess.CalledProcessError):
            _PERFORMANCE_CPU_TOTAL = 0
        return _PERFORMANCE_CPU_TOTAL


_THREADS_DEFAULT: bool = threading_supported() and not (
    hasattr(sys, "_is_gil_enabled") and sys._is_gil_enabled()
)


def _max_threads() -> int:
    if not threading_supported():
        return 0
    cpu_total = _cpu_total()
    performance_total = _performance_cpu_total()
    if performance_total > 0:
        return performance_total
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


def _pool_for_target_locked(
    target: int,
) -> tuple[ThreadPoolExecutor, ThreadPoolExecutor | None]:
    """Return a pool for *target* while holding ``_THREAD_POOL_LOCK``."""

    global _THREAD_POOL
    global _THREAD_POOL_WORKERS

    if _THREAD_POOL is not None and _THREAD_POOL_WORKERS == target:
        return _THREAD_POOL, None

    old_pool = _THREAD_POOL
    _THREAD_POOL = ThreadPoolExecutor(
        max_workers=target,
        thread_name_prefix=_POOL_NAME,
    )
    _THREAD_POOL_WORKERS = target
    return _THREAD_POOL, old_pool


def ensure_thread_pool(max_workers: int | None = None) -> ThreadPoolExecutor:
    """Return the shared executor, creating or resizing it if required."""

    with _THREAD_POOL_LOCK:
        target = _determine_worker_count(
            max_workers if max_workers is not None else _THREAD_POOL_WORKERS
        )
        pool, old_pool = _pool_for_target_locked(target)

    # Do not wait for old workers while holding the state lock.  In particular,
    # this avoids making executor shutdown depend on callbacks that inspect the
    # pool configuration.
    if old_pool is not None:
        old_pool.shutdown(wait=True)
    return pool


@contextmanager
def _thread_pool_submission(
    max_workers: int | None = None,
) -> Iterator[tuple[ThreadPoolExecutor, int]]:
    """Lease the pool state while a caller submits a batch of work.

    The lease is deliberately limited to executor acquisition and submission.
    Waiting for futures while holding the lock could deadlock if user work
    re-enters pool configuration.  Reconfiguration cannot shut down the leased
    executor until the context exits, so every submission is accepted by a live
    executor.
    """

    old_pool: ThreadPoolExecutor | None = None
    try:
        with _THREAD_POOL_LOCK:
            target = _determine_worker_count(
                max_workers if max_workers is not None else _THREAD_POOL_WORKERS
            )
            pool, old_pool = _pool_for_target_locked(target)
            yield pool, target
    finally:
        if old_pool is not None:
            old_pool.shutdown(wait=True)


def submit_thread_pool_tasks(
    tasks: list[Callable[[], object]], *, max_workers: int | None = None
) -> tuple[list[object], int]:
    """Submit *tasks* atomically with respect to pool replacement."""

    with _thread_pool_submission(max_workers) as (pool, workers):
        futures = [pool.submit(task) for task in tasks]
    return futures, workers


def configure_thread_pool(
    *, max_workers: int | None = None, preload: bool = False
) -> int:
    """Set the shared executor size used by :func:`parallel_map`.

    Returns the effective worker count after applying the update.
    """

    global _THREAD_POOL
    global _THREAD_POOL_WORKERS

    workers = _determine_worker_count(max_workers)

    with _THREAD_POOL_LOCK:
        _THREAD_POOL_WORKERS = workers
        pool = _THREAD_POOL
        _THREAD_POOL = None

    if pool is not None:
        pool.shutdown(wait=True)

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
    with _THREAD_POOL_LOCK:
        if _THREAD_POOL_WORKERS is None:
            _THREAD_POOL_WORKERS = _determine_worker_count(None)
        return _THREAD_POOL_WORKERS


def configure_threads(
    *, enabled: bool | None = None, threshold: int | None = None
) -> bool:
    """Adjust the global threading defaults and/or auto threshold."""

    global _THREADS_DEFAULT
    global _THREAD_AUTO_THRESHOLD

    with _THREAD_POOL_LOCK:
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
    with _THREAD_POOL_LOCK:
        return _THREADS_DEFAULT


def get_auto_threshold() -> int:
    with _THREAD_POOL_LOCK:
        return _THREAD_AUTO_THRESHOLD


atexit.register(shutdown_thread_pool)


__all__ = [
    "configure_thread_pool",
    "configure_threads",
    "ensure_thread_pool",
    "get_auto_threshold",
    "get_thread_default",
    "get_thread_pool_size",
    "shutdown_thread_pool",
    "submit_thread_pool_tasks",
    "threading_supported",
]
