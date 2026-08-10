# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

from __future__ import annotations

import concurrent.futures
import gc
import importlib
import threading
import tracemalloc
import weakref

import pcre_ext_c

import pcre
from pcre import cache as cache_mod
from pcre import pcre as pcre_mod
from pcre import re_compat


def test_explicit_thread_policies_have_distinct_stable_wrappers() -> None:
    pcre.clear_cache()
    enabled = pcre.compile("policy", flags=int(pcre.Flag.THREADS))
    disabled = pcre.compile("policy", flags=int(pcre.Flag.NO_THREADS))

    assert enabled is not disabled
    assert enabled._pattern is disabled._pattern
    assert enabled.thread_mode == "enabled"
    assert disabled.thread_mode == "disabled"

    assert pcre.compile("policy", flags=int(pcre.Flag.THREADS)) is enabled
    assert enabled.thread_mode == "enabled"
    assert disabled.thread_mode == "disabled"


def test_concurrent_thread_policy_compiles_cannot_clobber_each_other() -> None:
    barrier = threading.Barrier(2)

    def compile_with(flag: pcre.Flag) -> pcre.Pattern:
        barrier.wait(timeout=5)
        result = pcre.compile("concurrent-policy", flags=int(flag))
        for _ in range(1000):
            expected = "enabled" if flag == pcre.Flag.THREADS else "disabled"
            assert result.thread_mode == expected
        return result

    pcre.clear_cache()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        enabled_future = executor.submit(compile_with, pcre.Flag.THREADS)
        disabled_future = executor.submit(compile_with, pcre.Flag.NO_THREADS)
        enabled = enabled_future.result()
        disabled = disabled_future.result()

    assert enabled is not disabled
    assert enabled.thread_mode == "enabled"
    assert disabled.thread_mode == "disabled"


def test_module_cache_tracks_thread_default_without_mutating_old_pattern() -> None:
    threads_mod = importlib.import_module("pcre.threads")
    original = threads_mod.get_thread_default()
    pcre.clear_cache()
    try:
        pcre.configure_threads(enabled=False)
        disabled = pcre.search("module-policy", "module-policy").re
        pcre.configure_threads(enabled=True)
        automatic = pcre.search("module-policy", "module-policy").re
    finally:
        pcre.configure_threads(enabled=original)
        pcre.clear_cache()

    assert automatic is not disabled
    assert automatic._pattern is disabled._pattern
    assert disabled.thread_mode == "disabled"
    assert automatic.thread_mode == "auto"


def test_cache_limit_zero_disables_all_high_level_helper_caches() -> None:
    original = cache_mod.get_cache_limit()
    try:
        pcre.set_cache_limit(0)
        pcre.clear_cache()
        first = pcre.search("uncached-module", "uncached-module").re
        second = pcre.search("uncached-module", "uncached-module").re
        assert first is not second
        assert pcre_mod._local_cache("cache") == {}
        assert pcre_mod._module_cache_size() == 0
        assert pcre_mod._replacement_cache_size() == 0
    finally:
        pcre.set_cache_limit(original)
        pcre.clear_cache()


def test_limit_change_during_thread_local_compile_cannot_repopulate_cache(
    monkeypatch,
) -> None:
    original = cache_mod.get_cache_limit()
    try:
        cache_mod.set_cache_limit(2)
        cache_mod.clear_cache()
        monkeypatch.setattr(
            cache_mod._pcre2,
            "compile",
            lambda pattern, *, flags=0, jit=False: pattern,
        )

        def disable_cache(raw):
            cache_mod.set_cache_limit(0)
            return raw

        assert (
            cache_mod._cached_compile_thread_local(
                "reentrant-limit", 0, disable_cache, jit=False
            )
            == "reentrant-limit"
        )
        assert cache_mod._THREAD_LOCAL.pattern_cache == {}
    finally:
        cache_mod.set_cache_limit(original)
        pcre.clear_cache()


def test_none_limit_uses_hard_entry_ceiling() -> None:
    original = cache_mod.get_cache_limit()
    try:
        pcre.set_cache_limit(None)
        pcre.clear_cache()
        for index in range(cache_mod._HARD_CACHE_ENTRY_LIMIT + 32):
            pcre.compile(f"bounded-{index}")
        assert len(pcre_mod._local_cache("cache")) == cache_mod._HARD_CACHE_ENTRY_LIMIT
        assert (
            len(cache_mod._THREAD_LOCAL.pattern_cache)
            == cache_mod._HARD_CACHE_ENTRY_LIMIT
        )
    finally:
        pcre.set_cache_limit(original)
        pcre.clear_cache()


def test_oversized_patterns_are_not_retained_by_python_or_c_caches() -> None:
    source = "(?#" + "x" * cache_mod._MAX_CACHE_INPUT_UNITS + ")a"
    pcre.clear_cache()
    first = pcre.compile(source)
    second = pcre.compile(source)
    assert first is not second
    assert first._pattern is not second._pattern
    assert pcre_mod._local_cache("cache") == {}
    assert cache_mod._THREAD_LOCAL.pattern_cache == {}

    raw_first = pcre_ext_c.compile(source, jit=False)
    raw_second = pcre_ext_c.compile(source, jit=False)
    assert raw_first is not raw_second

    flagged_first = pcre.compile(source, flags=int(pcre.Flag.CASELESS))
    flagged_second = pcre.compile(source, flags=int(pcre.Flag.CASELESS))
    assert flagged_first is not flagged_second
    assert pcre_mod._local_cache("flagged_cache") == {}

    assert pcre.search(source, "a") is not None
    assert pcre_mod._module_cache_size() == 0


def test_oversized_templates_are_not_retained() -> None:
    pattern = pcre.compile("(x)")
    match = pattern.fullmatch("x")
    assert match is not None
    template = "y" * cache_mod._MAX_CACHE_INPUT_UNITS + r"\1"

    pcre_mod._cached_replacement_parts.cache_clear()
    assert pattern.sub(template, "x") == "y" * cache_mod._MAX_CACHE_INPUT_UNITS + "x"
    assert pcre_mod._replacement_cache_size() == 0

    re_compat._cached_expand_template.cache_clear()
    assert match.expand(template) == "y" * cache_mod._MAX_CACHE_INPUT_UNITS + "x"
    assert re_compat._expand_template_cache_size() == 0


def test_helper_pattern_wrappers_do_not_cross_thread_scope() -> None:
    barrier = threading.Barrier(2)

    def worker() -> pcre.Pattern:
        barrier.wait(timeout=5)
        return pcre.search("thread-scope", "thread-scope").re

    pcre.clear_cache()
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        first, second = executor.map(lambda _: worker(), range(2))
    assert first is not second


def test_clear_cache_invalidates_a_live_workers_direct_cache() -> None:
    def worker() -> pcre.Pattern:
        return pcre.compile("worker-generation")

    with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
        first = executor.submit(worker).result()
        pcre.clear_cache()
        second = executor.submit(worker).result()
    assert first is not second


def test_cache_control_registry_does_not_retain_reloaded_callbacks() -> None:
    def callback() -> None:
        pass

    callback_ref = weakref.ref(callback)
    cache_mod.register_cache_control(callback)
    assert callback in cache_mod._CACHE_CONTROL_CALLBACKS
    del callback
    gc.collect()
    assert callback_ref() is None


def test_module_uncached_compile_uses_its_configuration_snapshot(
    monkeypatch,
) -> None:
    base = pcre.compile("snapshot-base", flags=int(pcre.Flag.NO_JIT))
    observed: list[tuple[str, int, bool]] = []

    def fake_cached_compile(pattern, flags, wrapper, *, jit):
        observed.append((pattern, flags, jit))
        return base

    monkeypatch.setattr(pcre_mod, "cached_compile", fake_cached_compile)
    monkeypatch.setattr(
        pcre_mod,
        "_apply_regex_compat",
        lambda pattern, enabled: f"{pattern}:{enabled}",
    )
    compiled = pcre_mod._module_pattern_uncached(
        "snapshot", False, True, pcre_mod._THREAD_MODE_ENABLED
    )

    assert observed == [
        (
            "snapshot:True",
            pcre_ext_c.PCRE2_UTF | pcre_ext_c.PCRE2_UCP,
            False,
        )
    ]
    assert compiled is not base
    assert compiled._pattern is base._pattern
    assert compiled.thread_mode == "enabled"


def test_groups_does_not_retain_large_materialized_capture() -> None:
    captured = "x" * (1024 * 1024)
    subject = "[" + captured + "]"
    match = pcre.compile(r"\[(.*)\]").fullmatch(subject)
    assert match is not None

    tracemalloc.start()
    try:
        groups = match.groups()
        assert groups == (captured,)
        del groups
        gc.collect()
        retained, _ = tracemalloc.get_traced_memory()
    finally:
        tracemalloc.stop()

    assert retained < 16 * 1024


def test_groups_is_safe_for_concurrent_free_threaded_reads() -> None:
    match = pcre.compile("(a)(b)?").fullmatch("ab")
    assert match is not None

    def read_groups() -> None:
        for _ in range(5000):
            assert match.groups() == ("a", "b")

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(lambda _: read_groups(), range(8)))
