# SPDX-FileCopyrightText: 2026 ModelCloud.ai
# SPDX-FileCopyrightText: 2026 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Match resource limits and thread detaching for long-running matches (#129)."""

from __future__ import annotations

import gc
import sys
import threading
import time

import pytest

import pcre
from pcre import Flag

# ``[^q]*[QR]`` against a run of ``a`` never matches and backtracks O(n) at each
# of the n start positions, so the total work is O(n^2) while every individual
# start position stays well below PCRE2's default match limit.
_QUADRATIC_PATTERN = r"[^q]*[QR]"


@pytest.fixture(autouse=True)
def _restore_limits():
    yield
    pcre.configure_match_limits(match_limit=0, depth_limit=0, heap_limit=0)


def test_defaults_are_reported():
    limits = pcre.configure_match_limits()
    assert set(limits) == {"match_limit", "depth_limit", "heap_limit"}
    assert all(isinstance(value, int) and value > 0 for value in limits.values())


def test_update_and_reset_round_trip():
    defaults = pcre.configure_match_limits()
    updated = pcre.configure_match_limits(match_limit=1234, heap_limit=4096)
    assert updated["match_limit"] == 1234
    assert updated["heap_limit"] == 4096
    assert updated["depth_limit"] == defaults["depth_limit"]
    # ``None`` keeps the current value.
    assert pcre.configure_match_limits(depth_limit=None)["match_limit"] == 1234
    assert pcre.configure_match_limits(match_limit=0, heap_limit=0) == defaults


@pytest.mark.parametrize("bad", [-1, 2**32, 10**30])
def test_out_of_range_values_rejected(bad):
    with pytest.raises(ValueError):
        pcre.configure_match_limits(match_limit=bad)


@pytest.mark.parametrize("bad", ["10", 1.5, True])
def test_non_int_values_rejected(bad):
    with pytest.raises(TypeError):
        pcre.configure_match_limits(match_limit=bad)


def test_positional_arguments_rejected():
    with pytest.raises(TypeError):
        pcre.pcre_ext_c.configure_match_limits(100)


@pytest.mark.parametrize("flags", [0, Flag.NO_JIT], ids=["jit", "no_jit"])
@pytest.mark.parametrize(
    "operation",
    ["search", "match", "fullmatch", "findall", "finditer", "split", "sub", "subn"],
)
def test_match_limit_applies_to_every_entry_point(flags, operation):
    subject = "a" * 4000
    pattern = pcre.compile(_QUADRATIC_PATTERN, flags)
    if operation == "match" or operation == "fullmatch":
        # Anchored at one start position: needs a tighter limit to trip.
        pcre.configure_match_limits(match_limit=10)
    else:
        pcre.configure_match_limits(match_limit=1000)

    calls = {
        "search": lambda: pattern.search(subject),
        "match": lambda: pattern.match(subject),
        "fullmatch": lambda: pattern.fullmatch(subject),
        "findall": lambda: pattern.findall(subject),
        "finditer": lambda: list(pattern.finditer(subject)),
        "split": lambda: pattern.split(subject),
        "sub": lambda: pattern.sub("x", subject),
        "subn": lambda: pattern.subn("x", subject),
    }
    with pytest.raises(pcre.PcreError) as excinfo:
        calls[operation]()
    assert "limit" in str(excinfo.value).lower()


def test_limits_do_not_affect_cheap_matches():
    pcre.configure_match_limits(match_limit=1000)
    pattern = pcre.compile(r"(\w+)@(\w+)\.com")
    assert pattern.search("mail bob@example.com now").groups() == ("bob", "example")
    assert pattern.findall("a@b.com c@d.com") == [("a", "b"), ("c", "d")]
    assert pattern.sub(r"\2", "a@b.com") == "b"


def test_reset_restores_unbounded_behaviour():
    pattern = pcre.compile(_QUADRATIC_PATTERN, Flag.NO_JIT)
    subject = "a" * 2000
    pcre.configure_match_limits(match_limit=100)
    with pytest.raises(pcre.PcreError):
        pattern.search(subject)
    pcre.configure_match_limits(match_limit=0)
    assert pattern.search(subject) is None


@pytest.mark.skipif(
    not hasattr(sys, "_is_gil_enabled") or sys._is_gil_enabled(),
    reason="stop-the-world detaching is only observable on free-threaded builds",
)
def test_long_match_on_short_subject_does_not_block_gc():
    # Well below the 256KB GIL-release threshold used on GIL builds.
    subject = "a" * 16000
    pattern = pcre.compile(_QUADRATIC_PATTERN, Flag.NO_JIT)

    # gc.collect() itself gets slower as the test session's heap grows, so
    # compare the stall against an uncontended baseline taken first.
    baseline = 0.0
    for _ in range(3):
        start = time.perf_counter()
        gc.collect()
        baseline = max(baseline, time.perf_counter() - start)

    finished = threading.Event()
    elapsed: list[float] = []

    def worker():
        start = time.perf_counter()
        assert pattern.search(subject) is None
        elapsed.append(time.perf_counter() - start)
        finished.set()

    thread = threading.Thread(target=worker)
    thread.start()
    time.sleep(0.05)
    worst = 0.0
    while not finished.is_set():
        start = time.perf_counter()
        gc.collect()
        worst = max(worst, time.perf_counter() - start)
        time.sleep(0.005)
    thread.join()

    assert elapsed and elapsed[0] > 0.2, "match finished too quickly to measure"
    # Before the fix gc.collect() waited for the entire match.
    assert worst < baseline * 3 + elapsed[0] / 4, (worst, baseline, elapsed[0])
