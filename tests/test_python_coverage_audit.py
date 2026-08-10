# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0

"""Focused statement and branch coverage for Python compatibility fallbacks."""

from __future__ import annotations

import importlib
import importlib.util
import re
import sys
from threading import RLock
from types import SimpleNamespace
from typing import Any, ClassVar

import pcre_ext_c
import pytest

import pcre
import pcre._stdlib_re as stdlib_re
import pcre.cache as cache_mod
import pcre.pcre as pcre_mod
import pcre.re_compat as compat

error_mod = importlib.import_module("pcre.error")


def test_stdlib_parser_fallback(monkeypatch: pytest.MonkeyPatch) -> None:
    # Python 3.10 does not expose ``re._parser`` at module scope, while newer
    # releases do.  Either state should exercise our fallback loader.
    monkeypatch.delattr(re, "_parser", raising=False)
    parser = stdlib_re._load_parser()
    assert callable(parser.parse)


def test_stdlib_parser_exported_path(monkeypatch: pytest.MonkeyPatch) -> None:
    exported = object()
    monkeypatch.setattr(stdlib_re._std_re, "_parser", exported, raising=False)
    assert stdlib_re._load_parser() is exported


def test_flat_replacement_conversion_paths() -> None:
    assert pcre_mod._pcre2_replacement_from_parsed([1, b"$"], True) == b"\\g<1>$$"
    assert pcre_mod._pcre2_replacement_from_parsed([1, "$"], False) == r"\g<1>$$"


def test_replacement_template_cache_reuses_and_clears(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    pattern = pcre.compile(r"(x)")
    pcre_mod._cached_replacement_parts.cache_clear()
    original = pcre_mod._parser.parse_template
    calls = 0

    def counted_parse(template: Any, state: Any) -> Any:
        nonlocal calls
        calls += 1
        return original(template, state)

    monkeypatch.setattr(pcre_mod._parser, "parse_template", counted_parse)
    assert pattern.sub(r"[\1]", "x") == "[x]"
    assert pattern.sub(r"[\1]", "x") == "[x]"
    assert calls == 1

    pcre.clear_cache()
    assert pattern.sub(r"[\1]", "x") == "[x]"
    assert calls == 2


def test_expand_template_cache_reuses_and_clears(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    match = pcre.compile(r"(?P<x>x)").fullmatch("x")
    assert match is not None
    compat._cached_expand_template.cache_clear()
    original = compat._parser.parse_template
    calls = 0

    def counted_parse(template: Any, state: Any) -> Any:
        nonlocal calls
        calls += 1
        return original(template, state)

    monkeypatch.setattr(compat._parser, "parse_template", counted_parse)
    assert match.expand(r"[\g<x>]") == "[x]"
    assert match.expand(r"[\g<x>]") == "[x]"
    assert calls == 1

    pcre.clear_cache()
    assert match.expand(r"[\g<x>]") == "[x]"
    assert calls == 2


def test_expand_template_legacy_groupindex_falls_back() -> None:
    class LegacyGroupIndex(dict[str, int]):
        def items(self) -> Any:
            raise AttributeError("legacy mapping has no items snapshot")

    pattern = type(
        "LegacyPattern", (), {"groups": 1, "groupindex": LegacyGroupIndex()}
    )()
    match = compat.Match(
        pattern,
        type("RawMatch", (), {"group": lambda self, index: "x"})(),
        "x",
        0,
        1,
    )
    assert match.expand(r"[\1]") == "[x]"


def test_expand_render_single_capture_fast_shapes() -> None:
    class RawMatch:
        def group(self, index: int) -> str:
            assert index == 1
            return "x"

    raw = RawMatch()
    assert (
        compat.render_template(([(0, 1)], [None]), raw, is_bytes=False, empty="") == "x"
    )
    assert (
        compat.render_template(
            ([(1, 1)], ["[", None, "]"]), raw, is_bytes=False, empty=""
        )
        == "[x]"
    )
    assert compat.render_template([1], raw, is_bytes=False, empty="") == "x"
    assert compat.render_template(["[", 1, "]"], raw, is_bytes=False, empty="") == "[x]"

    class TwoGroupRawMatch:
        def group(self, index: int) -> str:
            return "x" if index == 1 else "y"

    assert (
        compat.render_template(
            ["[", 1, "-", 2, "]"],
            TwoGroupRawMatch(),
            is_bytes=False,
            empty="",
        )
        == "[x-y]"
    )


def test_default_compile_cache_is_thread_local_and_tracks_thread_mode(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    pcre.clear_cache()
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)
    first = pcre.compile("compile-cache")
    assert pcre.compile("compile-cache") is first
    assert first.thread_mode == pcre_mod._THREAD_MODE_DISABLED

    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: True)
    assert pcre.compile("compile-cache") is first
    assert first.thread_mode == pcre_mod._THREAD_MODE_AUTO

    pcre.clear_cache()
    assert pcre.compile("compile-cache") is not first

    original_limit = cache_mod.get_cache_limit()
    try:
        cache_mod.set_cache_limit(0)
        pcre.clear_cache()
        uncached = pcre.compile("compile-cache-disabled")
        assert pcre.compile("compile-cache-disabled") is not uncached
    finally:
        cache_mod.set_cache_limit(original_limit)
        pcre.clear_cache()


def test_compile_legacy_default_fallback_and_subject_length(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    backend = pcre_mod._pcre2.compile("x", jit=False)

    def fake_cached(*args: Any, **kwargs: Any) -> pcre_mod.Pattern:
        return pcre_mod.Pattern(backend)

    monkeypatch.setattr(pcre_mod, "cached_compile", fake_cached)
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)
    compiled = pcre_mod.compile(bytearray(b"x"))
    assert compiled.thread_mode == pcre_mod._THREAD_MODE_DISABLED
    assert (
        pcre_mod.compile(bytearray(b"x"), flags=int(pcre.Flag.THREADS)).thread_mode
        == pcre_mod._THREAD_MODE_ENABLED
    )
    assert (
        pcre_mod.compile(bytearray(b"x"), flags=int(pcre.Flag.NO_THREADS)).thread_mode
        == pcre_mod._THREAD_MODE_DISABLED
    )
    assert (
        pcre_mod.compile(bytearray(b"x"), flags=int(pcre.Flag.CASELESS)).thread_mode
        == pcre_mod._THREAD_MODE_DISABLED
    )
    assert pcre_mod._subject_length(bytearray(b"x")) == 1


def test_flagged_builtin_compile_cache_tracks_mode_and_limits(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    pcre.clear_cache()
    pcre_mod._DEFAULT_COMPILE_LOCAL.flagged_cache = None
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: True)
    flags = int(pcre.Flag.CASELESS)
    first = pcre.compile("flag-cache", flags=flags)
    assert pcre.compile("flag-cache", flags=flags) is first
    assert first.thread_mode == pcre_mod._THREAD_MODE_AUTO
    enum_first = pcre.compile("enum-cache", flags=pcre.Flag.CASELESS)
    assert pcre.compile("enum-cache", flags=pcre.Flag.CASELESS) is enum_first

    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)
    assert pcre.compile("flag-cache", flags=flags) is first
    assert first.thread_mode == pcre_mod._THREAD_MODE_DISABLED

    enabled = pcre.compile("flag-enabled", flags=int(pcre.Flag.THREADS))
    assert enabled.thread_mode == pcre_mod._THREAD_MODE_ENABLED

    original_limit = cache_mod.get_cache_limit()
    try:
        cache_mod.set_cache_limit(1)
        pcre.clear_cache()
        pcre.compile("flag-one", flags=flags)
        pcre.compile("flag-two", flags=flags)

        cache_mod.set_cache_limit(0)
        pcre.clear_cache()
        uncached = pcre.compile("flag-cache-disabled", flags=flags)
        assert pcre.compile("flag-cache-disabled", flags=flags) is not uncached
    finally:
        cache_mod.set_cache_limit(original_limit)
        pcre.clear_cache()


def test_module_helpers_accept_project_flag_enum_zero() -> None:
    assert pcre.match("x", "x", pcre.Flag(0)) is not None
    assert pcre.findall("x", "xx", pcre.Flag(0)) == ["x", "x"]
    assert pcre.split("x", "xx", flags=pcre.Flag(0)) == ["", "", ""]
    assert pcre.sub("x", "y", "xx", flags=pcre.Flag(0)) == "yy"


def test_replacement_fallbacks_cover_subclasses_and_bounded_templates() -> None:
    class DerivedPattern(pcre_mod.Pattern):
        pass

    backend = pcre_mod._pcre2.compile("(x)", jit=False)
    derived = DerivedPattern(backend)
    assert derived.sub(r"[\1]", "x") == "[x]"
    assert derived.sub(r"[\1]", "x", count=1) == "[x]"


def test_module_fast_dispatch_falls_back_for_legacy_backends(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class LegacyPattern:
        _is_c_pattern = True
        _pattern = object()

        def match(self, value: Any) -> str:
            return f"match:{value}"

        def search(self, value: Any) -> str:
            return f"search:{value}"

        def fullmatch(self, value: Any) -> str:
            return f"full:{value}"

        def finditer(self, value: Any) -> list[str]:
            return [f"iter:{value}"]

        def findall(self, value: Any) -> list[str]:
            return [f"all:{value}"]

        def split(self, value: Any, *, maxsplit: int) -> list[Any]:
            return [value, maxsplit]

        def subn(self, repl: Any, value: Any, *, count: int) -> tuple[Any, int]:
            return (f"sub:{repl}:{value}", count)

    legacy = LegacyPattern()
    monkeypatch.setattr(pcre_mod, "_module_compile", lambda *args: legacy)
    assert pcre_mod.match("x", "a") == "match:a"
    assert pcre_mod.search("x", "a") == "search:a"
    assert pcre_mod.fullmatch("x", "a") == "full:a"
    assert pcre_mod.finditer("x", "a") == ["iter:a"]
    assert pcre_mod.findall("x", "a") == ["all:a"]
    assert pcre_mod.split("x", "a") == ["a", 0]
    assert pcre_mod.subn("x", "r", "a") == ("sub:r:a", 0)


class _LegacyFastDispatchPattern:
    pattern = "x"
    groupindex: ClassVar[dict[str, int]] = {"g": 1}
    flags = 0
    capture_count = 1
    jit = False

    def match(self, *args: Any, **kwargs: Any) -> None:
        return None

    search = match
    fullmatch = match

    def finditer(self, *args: Any, **kwargs: Any) -> Any:
        return iter(())

    def findall(self, *args: Any, **kwargs: Any) -> list[Any]:
        return []

    def substitute(self, *args: Any, **kwargs: Any) -> tuple[str, int]:
        return ("", 0)


def test_legacy_c_dispatch_fallbacks(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "_CPattern", _LegacyFastDispatchPattern)
    pattern = pcre_mod.Pattern(_LegacyFastDispatchPattern())
    assert pattern.match("x") is None
    assert pattern.search("x") is None
    assert pattern.fullmatch("x") is None
    assert list(pattern.finditer("x")) == []
    assert pattern.findall("x") == []
    assert pattern.subn("literal", "x") == ("", 0)
    assert pattern.subn(r"\g<g>", "x") == ("", 0)

    type_error_backend = _LegacyFastDispatchPattern()

    def reject_keyword_call(*args: Any, **kwargs: Any) -> None:
        raise TypeError("legacy substitute signature")

    type_error_backend.substitute = reject_keyword_call  # type: ignore[method-assign]
    assert pcre_mod.Pattern(type_error_backend).subn("literal", "x") == ("x", 0)


def test_package_import_without_optional_simd_export() -> None:
    original = pcre_ext_c._cpu_ascii_vector_mode
    try:
        del pcre_ext_c._cpu_ascii_vector_mode
        reloaded = importlib.reload(pcre)
        assert "_cpu_ascii_vector_mode" not in reloaded.__all__
    finally:
        pcre_ext_c._cpu_ascii_vector_mode = original
        importlib.reload(pcre)


def test_cache_import_honours_global_environment(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("PYPCRE_CACHE_PATTERN_GLOBAL", "1")
    reloaded = importlib.reload(cache_mod)
    try:
        assert reloaded._CACHE_STRATEGY is reloaded._CacheStrategy.GLOBAL
    finally:
        monkeypatch.delenv("PYPCRE_CACHE_PATTERN_GLOBAL")
        importlib.reload(cache_mod)


class _ChangingZeroComparison:
    """Behave like a concurrently changed internal cache limit."""

    def __init__(self) -> None:
        self.comparisons = 0

    def __eq__(self, other: object) -> bool:
        assert other == 0
        self.comparisons += 1
        return self.comparisons > 1

    def __ne__(self, other: object) -> bool:
        return not self == other


def test_thread_cache_limit_rechecked_before_store(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    state = cache_mod._THREAD_LOCAL
    original_limit = state.cache_limit
    original_cache = state.pattern_cache
    state.cache_limit = _ChangingZeroComparison()  # type: ignore[assignment]
    state.pattern_cache = {}
    monkeypatch.setattr(cache_mod._pcre2, "compile", lambda *args, **kwargs: "compiled")
    try:
        result = cache_mod._cached_compile_thread_local(
            "pattern", 0, lambda value: value, jit=False
        )
        assert result == "compiled"
        assert state.pattern_cache == {}
    finally:
        state.cache_limit = original_limit
        state.pattern_cache = original_cache


class _SecondLookupMapping(dict[Any, Any]):
    def __init__(
        self, second_result: Any = None, *, second_raises: bool = False
    ) -> None:
        super().__init__()
        self.lookups = 0
        self.second_result = second_result
        self.second_raises = second_raises

    def __getitem__(self, key: Any) -> Any:
        self.lookups += 1
        if self.lookups == 1:
            raise KeyError(key)
        if self.second_raises:
            raise TypeError("key became unhashable")
        return self.second_result


@pytest.mark.parametrize("second_raises", [False, True])
def test_global_cache_handles_second_lookup_races(
    monkeypatch: pytest.MonkeyPatch, second_raises: bool
) -> None:
    state = cache_mod._GLOBAL_STATE
    original_cache = state.pattern_cache
    original_limit = state.cache_limit
    original_lock = state.lock
    existing = object()
    state.pattern_cache = _SecondLookupMapping(existing, second_raises=second_raises)
    state.cache_limit = None
    state.lock = RLock()
    monkeypatch.setattr(cache_mod._pcre2, "compile", lambda *args, **kwargs: object())
    try:
        result = cache_mod._cached_compile_global(
            "pattern", 0, lambda value: value, jit=False
        )
        if second_raises:
            assert result is not existing
        else:
            assert result is existing
    finally:
        state.pattern_cache = original_cache
        state.cache_limit = original_limit
        state.lock = original_lock


def test_unlimited_thread_cache_limit_branch() -> None:
    original_strategy = cache_mod._CACHE_STRATEGY
    original_limit = cache_mod._THREAD_LOCAL.cache_limit
    cache_mod._CACHE_STRATEGY = cache_mod._CacheStrategy.THREAD_LOCAL
    try:
        cache_mod.set_cache_limit(None)
        assert cache_mod.get_cache_limit() is None
    finally:
        cache_mod._THREAD_LOCAL.cache_limit = original_limit
        cache_mod._CACHE_STRATEGY = original_strategy


def _execute_error_module(
    name: str, backend: Any, monkeypatch: pytest.MonkeyPatch
) -> Any:
    monkeypatch.setitem(sys.modules, "pcre_ext_c", backend)
    spec = importlib.util.spec_from_file_location(name, error_mod.__file__)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_error_registration_fallback_and_documented_backend_type(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class FallbackError(Exception):
        pass

    fallback_module = _execute_error_module(
        "_pcre_error_fallback_coverage",
        SimpleNamespace(PcreError=FallbackError),
        monkeypatch,
    )
    fallback_type = fallback_module.ERRORS_BY_MACRO["PCRE2_ERROR_END_BACKSLASH"]
    assert fallback_type.__name__ == "PyErrorEndBackslash"
    assert fallback_type.__doc__ == (
        "PCRE2 error for PCRE2_ERROR_END_BACKSLASH (code 101)."
    )

    class DocumentedBackend:
        PcreError = FallbackError

        def __getattr__(self, name: str) -> type[FallbackError]:
            if not name.startswith("PcreError"):
                raise AttributeError(name)
            return type(name, (FallbackError,), {"__doc__": "existing doc"})

    documented_module = _execute_error_module(
        "_pcre_error_documented_coverage",
        DocumentedBackend(),
        monkeypatch,
    )
    documented_type = documented_module.ERRORS_BY_MACRO["PCRE2_ERROR_END_BACKSLASH"]
    assert documented_type.__doc__ == "existing doc"


class _EmptyGroupsMatch:
    def groups(self) -> tuple[()]:
        return ()


def test_group_hint_stays_unknown_for_match_without_captures() -> None:
    pattern = pcre.compile("a")
    pattern._groups_hint = None
    pattern._update_group_hint(_EmptyGroupsMatch())  # type: ignore[arg-type]
    assert pattern._groups_hint is None


def test_wrap_match_attaches_public_pattern() -> None:
    pattern = pcre.compile("a")
    raw = pattern._pattern.search("a")
    wrapped = pattern._wrap_match(raw, "a", 0, 1)
    assert wrapped is raw
    assert wrapped.re is pattern


class _LegacySplitPattern:
    pattern = "(,)"
    groupindex: ClassVar[dict[str, int]] = {}
    flags = 0
    capture_count = 1
    jit = False

    def split(self, subject: str, maxsplit: int = 0) -> list[str]:
        raise TypeError("legacy backend does not implement split")

    def finditer(
        self,
        subject: str,
        *args: Any,
        pos: int = 0,
        endpos: int = -1,
        options: int = 0,
        owner: Any = None,
    ) -> Any:
        if args:
            pos, endpos, options, owner = args
        del options, owner
        resolved_end = len(subject) if endpos < 0 else endpos
        return re.compile(self.pattern).finditer(subject, pos, resolved_end)


class _LegacyNoSplitPattern(_LegacySplitPattern):
    pattern = ","
    capture_count = 0

    def __getattribute__(self, name: str) -> Any:
        if name == "split":
            raise AttributeError(name)
        return super().__getattribute__(name)


def test_split_legacy_backend_fallback_and_limit() -> None:
    pattern = pcre_mod.Pattern(_LegacySplitPattern())  # type: ignore[arg-type]
    pattern._is_c_pattern = True
    assert pattern.split("a,b,c") == ["a", ",", "b", ",", "c"]
    assert pattern.split("a,b,c", maxsplit=1) == ["a", ",", "b,c"]

    no_split = pcre_mod.Pattern(_LegacyNoSplitPattern())  # type: ignore[arg-type]
    assert no_split.split("a,b") == ["a", "b"]


@pytest.mark.parametrize(
    ("pattern", "subject"),
    [("", "aé"), (b"", b"a\xff")],
)
def test_empty_pattern_split_fast_path_matches_re(
    pattern: str | bytes, subject: str | bytes
) -> None:
    compiled = pcre.compile(pattern)
    assert compiled.split(subject) == re.compile(pattern).split(subject)
    assert compiled.split(subject, maxsplit=1) == re.compile(pattern).split(
        subject, maxsplit=1
    )

    class Zero(int):
        pass

    assert compiled.split(subject, maxsplit=Zero(0)) == re.compile(pattern).split(
        subject
    )


def test_c_split_default_dispatch_preserves_results() -> None:
    pattern = pcre.compile(r"\s+")
    assert pattern.split("a b  c") == ["a", "b", "c"]
    bytes_pattern = pcre.compile(rb"\s+")
    assert bytes_pattern.split(b"a b  c") == [b"a", b"b", b"c"]


def test_c_literal_split_uses_builtin_only_for_safe_shape() -> None:
    text_pattern = pcre.compile(" ")
    assert text_pattern.split("a b c") == ["a", "b", "c"]
    assert text_pattern.split("a b c", maxsplit=1) == ["a", "b c"]
    assert text_pattern.split("a b c", maxsplit=-1) == ["a b c"]

    multi_pattern = pcre.compile(", ")
    assert multi_pattern.split("a, b, c") == ["a", "b", "c"]

    bytes_pattern = pcre.compile(b",")
    assert bytes_pattern.split(b"a,b,c") == [b"a", b"b", b"c"]

    # Regex metacharacters and non-default compile flags must retain PCRE2.
    assert pcre.compile(".").split("a.b") == ["", "", "", ""]
    assert pcre.compile("x", flags=pcre.Flag.CASELESS).split("Xx") == ["", "", ""]


def test_c_literal_subn_default_dispatch_preserves_results() -> None:
    pattern = pcre.compile(r"\w+")
    assert pattern.subn("X", "a b") == ("X X", 2)
    bytes_pattern = pcre.compile(rb"\w+")
    assert bytes_pattern.subn(b"X", b"a b") == (b"X X", 2)


def test_c_plain_literal_subn_uses_builtin_only_for_safe_shape() -> None:
    pattern = pcre.compile("foo")
    assert pattern.subn("bar", "foo foo") == ("bar bar", 2)
    assert pattern.subn("bar", "foo foo", count=1) == ("bar foo", 1)
    assert pattern.subn("bar", "foo foo", count=-1) == ("foo foo", 0)
    assert pattern.subn("bar", "no match") == ("no match", 0)

    bytes_pattern = pcre.compile(b"foo")
    assert bytes_pattern.subn(b"bar", b"foo foo") == (b"bar bar", 2)

    # Regex metacharacters and escaped replacement syntax stay on PCRE2.
    assert pcre.compile(".").subn("X", "ab") == ("XX", 2)
    assert pcre.compile("foo").subn(r"\\g<0>", "foo") == (r"\g<0>", 1)


def test_c_plain_literal_findall_uses_builtin_count_only_for_safe_shape() -> None:
    pattern = pcre.compile("foo")
    assert pattern.findall("foo foo") == ["foo", "foo"]
    assert pattern.findall("no match") == []
    assert pattern.findall("foo foo", pos=1) == ["foo"]

    bytes_pattern = pcre.compile(b"foo")
    assert bytes_pattern.findall(b"foo foo") == [b"foo", b"foo"]


def test_compile_existing_pattern_slow_path_and_jit_guards(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    pattern = pcre.compile("a")
    assert pcre.compile(pattern, flags=[]) is pattern

    monkeypatch.setattr(
        pcre_mod, "_extract_jit_override", lambda flags: not pattern.jit
    )
    with pytest.raises(ValueError, match="override jit"):
        pcre.compile(pattern, flags=[])

    raw = pattern._pattern
    with pytest.raises(ValueError, match="supply jit"):
        pcre.compile(raw, flags=[])


@pytest.mark.parametrize(
    ("thread_default", "expected_mode"),
    [
        (True, pcre_mod._THREAD_MODE_AUTO),
        (False, pcre_mod._THREAD_MODE_DISABLED),
    ],
)
def test_compile_raw_pattern_slow_path_thread_defaults(
    monkeypatch: pytest.MonkeyPatch, thread_default: bool, expected_mode: str
) -> None:
    raw = pcre.compile("a")._pattern
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: thread_default)
    wrapped = pcre.compile(raw, flags=[])
    assert wrapped.thread_mode == expected_mode


class _CompatRawMatch:
    def __init__(self, groups: tuple[Any, ...]) -> None:
        self._groups = groups

    def groups(self, default: Any = None) -> tuple[Any, ...]:
        return tuple(default if value is None else value for value in self._groups)

    def span(self, index: int = 0) -> tuple[int, int]:
        return (index, index + 1)


class _LookbehindPrefixProbe(str):
    """Exercise the later defensive lookbehind check for a str subclass."""

    def startswith(self, prefix: str, *args: int) -> bool:
        if prefix == "(?<":
            return False
        return super().startswith(prefix, *args)


def test_compat_type_detection_and_last_group_loop_branches() -> None:
    assert compat.is_bytes_like(object()) is False
    assert compat.is_capturing_group_start("(?<=x)", 0) is False
    assert compat.is_capturing_group_start(_LookbehindPrefixProbe("(?<=x)"), 0) is False

    no_groups = compat.Match(
        SimpleNamespace(groupindex={}), _CompatRawMatch(()), "", 0, 0
    )
    assert no_groups.lastindex is None
    assert no_groups.lastgroup is None

    pattern = SimpleNamespace(groupindex={"first": 1, "second": 2})
    second = compat.Match(pattern, _CompatRawMatch((None, "x")), "x", 0, 1)
    assert second.lastindex == 2
    assert second.lastgroup == "second"

    absent = compat.Match(
        SimpleNamespace(groupindex={"first": 1}),
        _CompatRawMatch((None, "x")),
        "x",
        0,
        1,
    )
    assert absent.lastgroup is None
