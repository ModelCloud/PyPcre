# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import threading
from typing import Any, List, Tuple

import pcre
from pcre.cache import _PATTERN_CACHE

_PATTERN_SUBJECTS: List[Tuple[Any, Any]] = [
    (r"(foo)(bar)", "foobar foo foobar"),
    (r"(?P<word>\\w+)", "Hello world from Python"),
    (r"(?:[A-Za-z]+-)*[A-Za-z]+", "well-known phrases"),
    (r"[A-Za-z]{2,3}", "ab cd efg hij"),
]


def test_clear_cache_threaded_flushes_all_caches() -> None:
    original_match_cache_size = pcre.cpcre2.get_match_data_cache_size()
    original_jit_cache_size = pcre.cpcre2.get_jit_stack_cache_size()
    original_jit_limits = pcre.cpcre2.get_jit_stack_limits()
    pcre.clear_cache()
    pcre.cpcre2.set_match_data_cache_size(16)
    pcre.cpcre2.set_jit_stack_cache_size(8)

    cases = [(pattern, subject, pcre.findall(pattern, subject)) for pattern, subject in _PATTERN_SUBJECTS]

    num_threads = 4
    warmup_barrier = threading.Barrier(num_threads + 1)
    resume_event = threading.Event()
    errors: List[BaseException] = []
    errors_lock = threading.Lock()

    def worker() -> None:
        try:
            for pattern, subject, expected in cases:
                assert pcre.findall(pattern, subject) == expected
            warmup_barrier.wait()
            if not resume_event.wait(timeout=5):
                raise AssertionError("resume signal not received")
            for _ in range(25):
                for pattern, subject, expected in cases:
                    assert pcre.findall(pattern, subject) == expected
        except BaseException as exc:  # pragma: no cover - surfaced via main thread assertions
            with errors_lock:
                errors.append(exc)
            raise

    threads = [threading.Thread(target=worker, name=f"cache-test-{i}") for i in range(num_threads)]

    try:
        for thread in threads:
            thread.start()

        try:
            warmup_barrier.wait(timeout=5)
        except threading.BrokenBarrierError as exc:  # pragma: no cover - indicates worker failure
            raise AssertionError("workers failed during warm-up") from exc

        assert len(_PATTERN_CACHE) >= len(cases)
        assert pcre.cpcre2.get_match_data_cache_count() > 0
        assert pcre.cpcre2.get_jit_stack_cache_count() > 0

        pcre.clear_cache()

        assert len(_PATTERN_CACHE) == 0
        assert pcre.cpcre2.get_match_data_cache_count() == 0
        assert pcre.cpcre2.get_jit_stack_cache_count() == 0

        resume_event.set()

        for thread in threads:
            thread.join()
    finally:
        resume_event.set()
        for thread in threads:
            thread.join(timeout=1)
        pcre.cpcre2.set_match_data_cache_size(original_match_cache_size)
        pcre.cpcre2.set_jit_stack_cache_size(original_jit_cache_size)
        pcre.cpcre2.set_jit_stack_limits(*original_jit_limits)
        pcre.clear_cache()

    assert not errors
