# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Deterministic GIL=0 thread-safety coverage for the C-extension fast paths."""

from __future__ import annotations

import os
import sys
import threading

import pytest

import pcre
import pcre.pcre as core
from pcre import Flag


def _gil_disabled() -> bool:
    try:
        return not sys._is_gil_enabled()
    except AttributeError:
        return False


@pytest.fixture
def _skip_without_gil_zero() -> None:
    if not _gil_disabled():
        pytest.skip("free-threaded GIL=0 interpreter required")


def _spawn_threads(target, count: int | None = None) -> list[threading.Thread]:
    count = count or max(1, os.cpu_count() or 4)
    threads = [threading.Thread(target=target) for _ in range(count)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    return threads


def _all_operations(pattern: pcre.Pattern, iterations: int) -> None:
    subject = "the quick brown fox jumps over the lazy dog"
    for _ in range(iterations):
        match = pattern.match(subject)
        assert match is not None and match.group(0) == "the"

        match = pattern.search(subject)
        assert match is not None and match.group(0) == "the"

        match = pattern.fullmatch(subject)
        assert match is None

        parts = pattern.split(subject)
        assert "the" in parts

        matches = list(pattern.finditer(subject))
        assert len(matches) == 9
        assert len(pattern.findall(subject)) == 9

        replaced, count = pattern.subn(r"[\1]", subject)
        assert isinstance(replaced, str) and count == 9
        assert isinstance(pattern.sub(r"[\1]", subject), str)


def test_gil_zero_shared_pattern_all_methods(_skip_without_gil_zero) -> None:
    pattern = pcre.compile(r"(\w+)", flags=Flag.THREADS)
    errors: list[str] = []
    lock = threading.Lock()

    def worker() -> None:
        try:
            _all_operations(pattern, iterations=250)
        except Exception as exc:
            with lock:
                errors.append(str(exc))

    _spawn_threads(worker, count=8)
    assert not errors


def test_gil_zero_compile_and_match_in_threads(_skip_without_gil_zero) -> None:
    errors: list[str] = []
    lock = threading.Lock()

    def worker() -> None:
        try:
            for _ in range(250):
                pattern = pcre.compile(r"(\w+)", flags=Flag.THREADS)
                match = pattern.match("hello world")
                assert match is not None and match.group(0) == "hello"
                match = pattern.search("hello world")
                assert match is not None and match.group(0) == "hello"
                match = pattern.fullmatch("hello")
                assert match is not None and match.group(0) == "hello"
        except Exception as exc:
            with lock:
                errors.append(str(exc))

    _spawn_threads(worker, count=8)
    assert not errors


def test_gil_zero_shared_finditer_is_serialized(_skip_without_gil_zero) -> None:
    subject = " ".join(f"word{i}" for i in range(1000))
    pattern = pcre.compile(r"\w+", flags=Flag.THREADS)
    iterator = pattern.finditer(subject)
    spans: list[tuple[int, int]] = []
    errors: list[str] = []
    lock = threading.Lock()

    def worker() -> None:
        try:
            while True:
                try:
                    match = next(iterator)
                except StopIteration:
                    return
                with lock:
                    spans.append(match.span())
        except Exception as exc:
            with lock:
                errors.append(str(exc))

    _spawn_threads(worker, count=8)
    expected = [match.span() for match in pattern.finditer(subject)]
    assert not errors
    assert sorted(spans) == expected


def test_gil_zero_pattern_thread_mode_reads_are_coherent(_skip_without_gil_zero) -> None:
    pattern = pcre.compile(r"a", flags=pcre.Flag.NO_THREADS)
    modes = {"disabled", "enabled", "auto"}
    errors: list[str] = []
    start = threading.Barrier(9)

    def writer() -> None:
        try:
            start.wait()
            for _ in range(500):
                pattern.enable_threads()
                pattern.disable_threads()
                pattern.enable_auto_threads()
        except Exception as exc:
            errors.append(str(exc))

    def reader() -> None:
        try:
            start.wait()
            for _ in range(2_000):
                assert pattern.thread_mode in modes
                assert isinstance(pattern.use_threads, bool)
        except Exception as exc:
            errors.append(str(exc))

    threads = [threading.Thread(target=writer)] + [
        threading.Thread(target=reader) for _ in range(8)
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert not errors


def test_gil_zero_defaults_snapshot_during_concurrent_compile(_skip_without_gil_zero) -> None:
    original_jit = core._DEFAULT_JIT
    original_compat = core._DEFAULT_COMPAT_REGEX
    errors: list[str] = []
    lock = threading.Lock()

    def record_error(exc: Exception) -> None:
        with lock:
            errors.append(str(exc))

    def reconfigure() -> None:
        try:
            for index in range(100):
                pcre.configure(jit=index % 2 == 0, compat_regex=index % 2 == 1)
        except Exception as exc:
            record_error(exc)

    def compile_repeatedly() -> None:
        try:
            for _ in range(250):
                compiled = pcre.compile(r"a")
                assert compiled.match("a") is not None
        except Exception as exc:
            record_error(exc)

    try:
        workers = [threading.Thread(target=reconfigure)] + [
            threading.Thread(target=compile_repeatedly) for _ in range(4)
        ]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        assert not errors
    finally:
        pcre.configure(jit=original_jit, compat_regex=original_compat)


def test_gil_zero_default_snapshot_never_contains_mixed_defaults(
    _skip_without_gil_zero,
) -> None:
    original_jit = core._DEFAULT_JIT
    original_compat = core._DEFAULT_COMPAT_REGEX
    expected = {(True, False), (False, True)}
    snapshots: list[tuple[bool, bool]] = []
    errors: list[str] = []
    start = threading.Barrier(9)

    def reconfigure() -> None:
        try:
            start.wait()
            for index in range(500):
                pcre.configure(jit=index % 2 == 0, compat_regex=index % 2 == 1)
        except Exception as exc:
            errors.append(str(exc))

    def read_snapshot() -> None:
        try:
            start.wait()
            for _ in range(5_000):
                snapshot = core._default_config_snapshot()
                assert snapshot in expected
                snapshots.append(snapshot)
        except Exception as exc:
            errors.append(str(exc))

    try:
        workers = [threading.Thread(target=reconfigure)] + [
            threading.Thread(target=read_snapshot) for _ in range(8)
        ]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        assert snapshots
        assert not errors
    finally:
        pcre.configure(jit=original_jit, compat_regex=original_compat)
