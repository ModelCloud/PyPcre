# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""In-process coverage tests for the global pattern-cache strategy."""

from __future__ import annotations

from typing import Any

import pcre.cache as cache_mod
import pytest


def test_env_flag_is_true() -> None:
    assert cache_mod._env_flag_is_true(None) is False
    assert cache_mod._env_flag_is_true("") is False
    assert cache_mod._env_flag_is_true("0") is False
    assert cache_mod._env_flag_is_true("false") is False
    assert cache_mod._env_flag_is_true("FALSE") is False
    assert cache_mod._env_flag_is_true("no") is False
    assert cache_mod._env_flag_is_true("NO") is False
    assert cache_mod._env_flag_is_true("1") is True
    assert cache_mod._env_flag_is_true("true") is True


def test_global_cached_compile(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[Any] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> Any:
        calls.append((pattern, flags, jit))
        return pattern

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    def wrapper(raw: Any) -> Any:
        return f"wrapped:{raw}"

    first = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    assert first == "wrapped:expr"
    assert len(calls) == 1

    second = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    assert second == first
    assert len(calls) == 1


def test_global_cache_clear_and_limits(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[Any] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> Any:
        calls.append(pattern)
        return pattern

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    def wrapper(raw: Any) -> Any:
        return raw

    cache_mod.cached_compile("a", 0, wrapper, jit=False)
    cache_mod.cached_compile("b", 0, wrapper, jit=False)
    assert len(fresh_state.pattern_cache) == 2

    cache_mod.clear_cache()
    assert len(fresh_state.pattern_cache) == 0

    cache_mod.set_cache_limit(1)
    assert cache_mod.get_cache_limit() == 1
    cache_mod.cached_compile("x", 0, wrapper, jit=False)
    cache_mod.cached_compile("y", 0, wrapper, jit=False)
    assert len(fresh_state.pattern_cache) == 1

    cache_mod.set_cache_limit(0)
    assert cache_mod.get_cache_limit() == 0
    assert len(fresh_state.pattern_cache) == 0

    cache_mod.cached_compile("z", 0, wrapper, jit=False)
    assert len(fresh_state.pattern_cache) == 0


def test_global_set_cache_limit_none(monkeypatch: pytest.MonkeyPatch) -> None:
    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    cache_mod.set_cache_limit(None)
    assert cache_mod.get_cache_limit() is None


def test_global_set_cache_limit_shrinks_existing_entries(monkeypatch: pytest.MonkeyPatch) -> None:
    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> Any:
        return pattern

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    def wrapper(raw: Any) -> Any:
        return raw

    cache_mod.cached_compile("a", 0, wrapper, jit=False)
    cache_mod.cached_compile("b", 0, wrapper, jit=False)
    assert len(fresh_state.pattern_cache) == 2

    cache_mod.set_cache_limit(1)
    assert len(fresh_state.pattern_cache) == 1


def test_global_cached_compile_handles_unhashable_key(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[Any] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> Any:
        calls.append(pattern)
        return pattern

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    def wrapper(raw: Any) -> Any:
        return raw

    first = cache_mod.cached_compile(["unhashable"], 0, wrapper, jit=False)
    second = cache_mod.cached_compile(["unhashable"], 0, wrapper, jit=False)
    assert first == second
    assert len(calls) == 2
    assert len(fresh_state.pattern_cache) == 0


def test_global_cached_compile_respects_limit_set_to_zero_after_compile(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[Any] = []

    def fake_compile(pattern: Any, *, flags: int = 0, jit: bool = False) -> Any:
        calls.append(pattern)
        return pattern

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    fresh_state = cache_mod._GlobalCacheState()
    monkeypatch.setattr(cache_mod, "_GLOBAL_STATE", fresh_state)
    monkeypatch.setattr(cache_mod, "_CACHE_STRATEGY", cache_mod._CacheStrategy.GLOBAL)

    def wrapper(raw: Any) -> Any:
        # Simulate another thread setting the limit to zero between the compile
        # and the cache insertion.
        fresh_state.cache_limit = 0
        return raw

    cache_mod.cached_compile("x", 0, wrapper, jit=False)
    assert len(fresh_state.pattern_cache) == 0
