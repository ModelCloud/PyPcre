# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Deterministic GIL=0 thread-safety coverage for the C-extension fast paths."""

from __future__ import annotations

import os
import sys
import threading

import pcre
import pytest
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
