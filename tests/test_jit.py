# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-License-Identifier: Apache-2.0

import types

import pcre
import pytest
from pcre import Flag
from pcre import pcre as core


_BAD_OPTION_SENTINEL = 0xFFFFFFFF


@pytest.fixture
def require_jit_backend():
    if not pcre.configure():
        pytest.skip("PCRE2 JIT support unavailable on this platform")
    yield


def _make_fake_cpattern(pattern_text: str, flags: int, jit: bool):
    return types.SimpleNamespace(
        pattern=pattern_text,
        groupindex={},
        flags=flags,
        match=lambda *args, **kwargs: None,
        search=lambda *args, **kwargs: None,
        fullmatch=lambda *args, **kwargs: None,
        jit=jit,
    )


def test_flag_jit_forces_enable(monkeypatch):
    captured = {}

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured["jit"] = jit
        return wrapper(_make_fake_cpattern(pattern, flags, jit))

    monkeypatch.setattr(core, "cached_compile", fake_cached)
    monkeypatch.setattr(core, "_DEFAULT_JIT", False)

    compiled = pcre.compile("expr", flags=Flag.JIT)

    assert captured["jit"] is True
    assert isinstance(compiled, pcre.Pattern)
    assert compiled.jit is True


def test_flag_no_jit_disables_when_default_enabled(monkeypatch):
    captured = {}

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured["jit"] = jit
        return wrapper(_make_fake_cpattern(pattern, flags, jit))

    monkeypatch.setattr(core, "cached_compile", fake_cached)
    monkeypatch.setattr(core, "_DEFAULT_JIT", True)

    compiled = pcre.compile("expr", flags=Flag.NO_JIT)

    assert captured["jit"] is False
    assert isinstance(compiled, pcre.Pattern)
    assert compiled.jit is False


def test_flag_conflict_raises():
    with pytest.raises(ValueError):
        pcre.compile("expr", flags=Flag.JIT | Flag.NO_JIT)


def test_configure_updates_default(monkeypatch):
    calls = []

    def fake_configure(*, jit=None):
        calls.append(jit)
        if jit is None:
            return False
        return jit

    monkeypatch.setattr(core._pcre2, "configure", fake_configure)
    monkeypatch.setattr(core, "_DEFAULT_JIT", True)

    assert pcre.configure(jit=False) is False
    assert core._DEFAULT_JIT is False
    assert calls == [False]

    calls.clear()
    assert pcre.configure() is False
    assert calls == [None]


def test_default_follows_configure(monkeypatch):
    captured = {}
    configure_calls = []

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured["jit"] = jit
        return wrapper(_make_fake_cpattern(pattern, flags, jit))

    def fake_configure(*, jit=None):
        configure_calls.append(jit)
        if jit is None:
            return False
        return jit

    monkeypatch.setattr(core, "cached_compile", fake_cached)
    monkeypatch.setattr(core, "_DEFAULT_JIT", True)
    monkeypatch.setattr(core._pcre2, "configure", fake_configure)

    pcre.configure(jit=False)

    compiled = pcre.compile("expr")

    assert captured["jit"] is False
    assert compiled.jit is False
    assert configure_calls == [False]

    pcre.configure(jit=True)


def test_existing_pattern_with_jit_flag_raises(monkeypatch):
    class DummyCPattern:
        def __init__(self):
            self.pattern = "expr"
            self.groupindex = {}
            self.flags = 0
            self.match = lambda *args, **kwargs: None
            self.search = lambda *args, **kwargs: None
            self.fullmatch = lambda *args, **kwargs: None
            self.jit = True

    pattern = core.Pattern(DummyCPattern())

    with pytest.raises(ValueError):
        pcre.compile(pattern, flags=Flag.NO_JIT)


def test_flag_no_jit_does_not_change_global_default(monkeypatch):
    captured = {}

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured.setdefault("jits", []).append(jit)
        return wrapper(_make_fake_cpattern(pattern, flags, jit))

    monkeypatch.setattr(core, "cached_compile", fake_cached)
    monkeypatch.setattr(core, "_DEFAULT_JIT", True)

    first = pcre.compile("expr")
    second = pcre.compile("expr", flags=Flag.NO_JIT)
    third = pcre.compile("expr")

    assert list(captured["jits"]) == [True, False, True]
    assert first.jit is True
    assert second.jit is False
    assert third.jit is True


def test_compile_with_jit_uses_backend(require_jit_backend):
    compiled = pcre.compile("a+", flags=Flag.JIT)

    assert isinstance(compiled, pcre.Pattern)
    assert compiled.jit is True
    assert compiled.match("aaa").group(0) == "aaa"


def test_clear_cache_discards_degraded_jit_state(require_jit_backend):
    pattern = pcre.compile("a+", flags=Flag.JIT)
    assert pattern.jit is True

    for _ in range(2):
        with pytest.raises(pcre.PcreError):
            pattern.match("aaa", options=_BAD_OPTION_SENTINEL)

    assert pattern.jit is False

    pcre.clear_cache()
    refresher = pcre.compile("a+", flags=Flag.JIT)
    assert refresher.jit is True

    alt = pcre.compile("b+", flags=Flag.JIT)
    assert alt.jit is True
