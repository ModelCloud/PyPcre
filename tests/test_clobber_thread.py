# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium


from __future__ import annotations

import os
import queue
import random
import threading
import time
import traceback
from dataclasses import dataclass
from typing import Optional

import pcre
import pytest
from tests import test_clobber as fuzz_core


_RUN_DURATION_SECONDS = 60.0
_MAX_THREAD_SPAWN = max(1, os.cpu_count() or 1)
_COMPILED_POOL_LIMIT = 256
_INBOX_DRAIN_LIMIT = 6


@dataclass(frozen=True)
class CompiledEntry:
    pattern_input: str | bytes
    flags: pcre.Flag
    compiled: pcre.Pattern
    is_bytes: bool


class SharedState:
    def __init__(self, deadline: float) -> None:
        self.deadline = deadline
        self.stop_event = threading.Event()
        self._failure_lock = threading.Lock()
        self._failures: list[str] = []
        self._compiled_lock = threading.Lock()
        self._compiled: list[CompiledEntry] = []
        self._workers_lock = threading.Lock()
        self._workers: list[Worker] = []
        self._operation_lock = threading.Lock()
        self._operation_count = 0

    def should_stop(self) -> bool:
        return self.stop_event.is_set() or time.monotonic() >= self.deadline

    def record_failure(self, message: str) -> None:
        with self._failure_lock:
            self._failures.append(message)
        self.stop_event.set()

    def failures(self) -> list[str]:
        with self._failure_lock:
            return list(self._failures)

    def has_failure(self) -> bool:
        with self._failure_lock:
            return bool(self._failures)

    def add_compiled(self, entry: CompiledEntry) -> None:
        with self._compiled_lock:
            self._compiled.append(entry)
            if len(self._compiled) > _COMPILED_POOL_LIMIT:
                self._compiled.pop(0)

    def sample_compiled(self, rng: random.Random) -> Optional[CompiledEntry]:
        with self._compiled_lock:
            if not self._compiled:
                return None
            return rng.choice(self._compiled)

    def register_worker(self, worker: Worker) -> None:
        with self._workers_lock:
            self._workers.append(worker)

    def workers_snapshot(self) -> list[Worker]:
        with self._workers_lock:
            return list(self._workers)

    def choose_peer(self, current: Worker, rng: random.Random) -> Optional[Worker]:
        with self._workers_lock:
            candidates = [worker for worker in self._workers if worker is not current]
        if not candidates:
            return None
        return rng.choice(candidates)

    def record_operation(self) -> None:
        with self._operation_lock:
            self._operation_count += 1

    @property
    def operation_count(self) -> int:
        with self._operation_lock:
            return self._operation_count


class Worker(threading.Thread):
    def __init__(self, worker_id: int, seed: int, shared: SharedState) -> None:
        super().__init__(name=f"clobber-worker-{worker_id}", daemon=True)
        self.worker_id = worker_id
        self.seed = seed
        self.shared = shared
        self.rng = random.Random(seed)
        self.local_entries: list[CompiledEntry] = []
        self.inbox: queue.Queue[CompiledEntry] = queue.Queue()

    def run(self) -> None:  # pragma: no cover - exercised via threading fuzz
        try:
            while not self.shared.should_stop():
                self._drain_inbox()
                if self.shared.should_stop():
                    break
                action = self.rng.random()
                executed = False
                if action < 0.3:
                    executed = self._compile_new()
                elif action < 0.6:
                    executed = self._exercise_local()
                elif action < 0.85:
                    executed = self._exercise_shared()
                else:
                    executed = self._handoff()
                if not executed:
                    time.sleep(self.rng.uniform(0.0005, 0.002))
            self._drain_inbox()
        except Exception as exc:  # pragma: no cover - unexpected failure should surface
            tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            self.shared.record_failure(
                f"worker crash: worker_id={self.worker_id} seed={self.seed} error={exc!r}\n{tb}"
            )

    def _drain_inbox(self) -> None:
        for _ in range(_INBOX_DRAIN_LIMIT):
            if self.shared.should_stop():
                return
            try:
                entry = self.inbox.get_nowait()
            except queue.Empty:
                return
            self.local_entries.append(entry)
            self._exercise_entry(entry)

    def _compile_new(self) -> bool:
        pattern_str = fuzz_core._random_pattern(self.rng)
        pattern_input, is_bytes = fuzz_core._maybe_to_bytes(pattern_str, self.rng)
        flags = fuzz_core._random_flags(self.rng, is_bytes)
        try:
            compiled = pcre.compile(pattern_input, flags=flags)
        except pcre.error:
            return False
        except Exception as exc:  # pragma: no cover - unexpected failure should surface
            tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            self.shared.record_failure(
                f"compile error: worker_id={self.worker_id} seed={self.seed} pattern={pattern_input!r} "
                f"flags={flags!r} error={exc!r}\n{tb}"
            )
            return False

        entry = CompiledEntry(pattern_input=pattern_input, flags=flags, compiled=compiled, is_bytes=is_bytes)
        self.local_entries.append(entry)
        self.shared.add_compiled(entry)
        self.shared.record_operation()
        if self.rng.random() < 0.7:
            self._exercise_entry(entry)
        if self.rng.random() < 0.3:
            self._handoff(entry)
        return True

    def _exercise_local(self) -> bool:
        if not self.local_entries:
            return False
        entry = self.rng.choice(self.local_entries)
        self._exercise_entry(entry)
        return True

    def _exercise_shared(self) -> bool:
        entry = self.shared.sample_compiled(self.rng)
        if entry is None:
            return False
        self._exercise_entry(entry)
        return True

    def _handoff(self, entry: Optional[CompiledEntry] = None) -> bool:
        if entry is None:
            if not self.local_entries:
                return False
            entry = self.rng.choice(self.local_entries)
        target = self.shared.choose_peer(self, self.rng)
        if target is None:
            return False
        target.inbox.put(entry)
        if self.rng.random() < 0.5:
            self._exercise_entry(entry)
        return True

    def _exercise_entry(self, entry: CompiledEntry) -> None:
        if self.shared.should_stop():
            return
        subject = fuzz_core._random_subject(self.rng, entry.is_bytes)
        try:
            fuzz_core._exercise_pattern(
                entry.compiled,
                entry.pattern_input,
                entry.flags,
                subject,
                self.rng,
            )
        except pcre.error:
            return
        except Exception as exc:  # pragma: no cover - unexpected failure should surface
            tb = "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            subject_type = "bytes" if entry.is_bytes else "str"
            self.shared.record_failure(
                "operation error: "
                f"worker_id={self.worker_id} seed={self.seed} pattern={entry.pattern_input!r} "
                f"subject_type={subject_type} flags={entry.flags!r} error={exc!r}\n{tb}"
            )
        else:
            self.shared.record_operation()


def test_randomized_thread_clobbering_ci_fuzz() -> None:
    seed = fuzz_core._system_seed()
    print(f"[test_clobber_thread] seed={seed}")
    rng = random.Random(seed)
    deadline = time.monotonic() + _RUN_DURATION_SECONDS
    shared = SharedState(deadline=deadline)

    worker_id = 0

    def start_worker() -> None:
        nonlocal worker_id
        worker_id += 1
        worker_seed = rng.getrandbits(64)
        worker = Worker(worker_id=worker_id, seed=worker_seed, shared=shared)
        shared.register_worker(worker)
        worker.start()

    max_threads_this_run = rng.randint(2 if _MAX_THREAD_SPAWN > 1 else 1, _MAX_THREAD_SPAWN)
    initial_threads = rng.randint(1, min(4, max_threads_this_run))
    for _ in range(initial_threads):
        start_worker()

    next_spawn = time.monotonic() + rng.uniform(0.01, 0.1)

    try:
        while not shared.should_stop():
            now = time.monotonic()
            if now >= deadline:
                break
            if len(shared.workers_snapshot()) < max_threads_this_run and now >= next_spawn:
                start_worker()
                next_spawn = now + rng.uniform(0.01, 0.1)
            else:
                time.sleep(min(0.01, max(0.0, deadline - now)))
    finally:
        shared.stop_event.set()
        for worker in shared.workers_snapshot():
            worker.join()

    failures = shared.failures()
    if failures:
        pytest.fail("; ".join(failures))

    assert shared.operation_count > 0
