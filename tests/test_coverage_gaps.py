# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Coverage tests for pcre.pcre corner cases missing from the main suites."""

from __future__ import annotations

from typing import Any

import pcre
import pcre.pcre as pcre_mod
import pytest


def test_compile_rejects_invalid_flags_type() -> None:
    with pytest.raises(TypeError):
        pcre.compile("a", flags="foo")
    with pytest.raises(TypeError):
        pcre.compile("a", flags=b"foo")
    with pytest.raises(TypeError):
        pcre.compile("a", flags=1.5)


def test_compile_rejects_conflicting_thread_flags() -> None:
    with pytest.raises(ValueError, match="cannot be combined"):
        pcre.compile("a", flags=pcre.Flag.THREADS | pcre.Flag.NO_THREADS)


def test_compile_pattern_instance_with_thread_flags() -> None:
    pattern = pcre.compile("a")

    pcre.compile(pattern, flags=pcre.Flag.THREADS)
    assert pattern.use_threads is True

    pcre.compile(pattern, flags=pcre.Flag.NO_THREADS)
    assert pattern.use_threads is False


def test_compile_pattern_instance_with_compat_flag_raises() -> None:
    pattern = pcre.compile("a")
    with pytest.raises(ValueError, match="COMPAT_UNICODE_ESCAPE"):
        pcre.compile(pattern, flags=pcre.Flag.COMPAT_UNICODE_ESCAPE)


def test_compile_pattern_instance_with_other_flags_raises() -> None:
    pattern = pcre.compile("a")
    with pytest.raises(ValueError, match="Cannot supply flags"):
        pcre.compile(pattern, flags=pcre.Flag.MULTILINE)


def test_compile_compiled_c_pattern_with_thread_flags(monkeypatch: pytest.MonkeyPatch) -> None:
    raw = pcre.compile("a")._pattern

    pcre.compile(raw, flags=pcre.Flag.THREADS)
    pcre.compile(raw, flags=pcre.Flag.NO_THREADS)

    with pytest.raises(ValueError, match="Cannot supply flags"):
        pcre.compile(raw, flags=pcre.Flag.MULTILINE)

    with pytest.raises(ValueError, match="COMPAT_UNICODE_ESCAPE"):
        pcre.compile(raw, flags=pcre.Flag.COMPAT_UNICODE_ESCAPE)


def test_compile_respects_disabled_thread_default(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)

    # Default thread mode disabled for string patterns.
    pattern = pcre.compile("a")
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_DISABLED

    # Explicit thread flag still works.
    pattern = pcre.compile("a", flags=pcre.Flag.THREADS)
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_ENABLED

    raw = pcre.compile("b")._pattern
    pattern = pcre.compile(raw)
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_DISABLED
    pattern = pcre.compile(raw, flags=pcre.Flag.NO_THREADS)
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_DISABLED


def test_compile_respects_disabled_thread_default_with_no_threads(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)
    pattern = pcre.compile("a", flags=pcre.Flag.NO_THREADS)
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_DISABLED


def test_match_methods_accept_memoryview() -> None:
    pattern = pcre.compile(br"\w+")
    data = memoryview(b"hello world")

    assert pattern.match(data).group(0) == b"hello"
    assert pattern.match(data, endpos=4).group(0) == b"hell"
    assert pattern.match(data, endpos=0) is None

    assert pattern.search(data).group(0) == b"hello"
    assert pattern.search(data, pos=6).group(0) == b"world"
    assert pattern.search(data, pos=6, endpos=7).group(0) == b"w"
    assert pattern.search(data, pos=6, endpos=6) is None

    full_pattern = pcre.compile(br"hello")
    assert full_pattern.fullmatch(data, endpos=5).group(0) == b"hello"
    assert full_pattern.fullmatch(data, endpos=4) is None
    assert full_pattern.fullmatch(data, endpos=0) is None

    assert pattern.findall(data) == [b"hello", b"world"]
    assert list(pattern.finditer(data)) != []


def test_no_match_with_memoryview_and_endpos() -> None:
    pattern = pcre.compile(br"\d+")
    data = memoryview(b"hello world")

    assert pattern.match(data) is None
    assert pattern.match(data, endpos=0) is None
    assert pattern.search(data) is None
    assert pattern.fullmatch(data) is None
    assert pattern.findall(data) == []
    assert list(pattern.finditer(data)) == []


def test_compat_match_path_used_when_attach_unavailable(monkeypatch: pytest.MonkeyPatch) -> None:
    """Force the pure-Python Match wrapper path used by older backends."""

    monkeypatch.setattr(pcre_mod, "_can_attach_match", lambda raw: False)

    pattern = pcre.compile(r"(a)(b)?")
    match = pattern.search("xay")
    assert match.group(0) == "a"
    assert match.group(1) == "a"
    assert match.group(2) is None
    assert match.groups() == ("a", None)
    assert match.start() == 1
    assert match.end() == 2
    assert match.span() == (1, 2)
    assert match.re is pattern
    assert match.string == "xay"
    assert match.pos == 0
    assert match.lastindex == 1
    assert match.lastgroup is None


def test_finditer_zero_length_pattern_advances() -> None:
    matches = list(pcre.finditer(r"", "abc"))
    assert len(matches) == 4


def test_module_template_function() -> None:
    with pytest.warns(DeprecationWarning):
        pattern = pcre.template("(a)")
    assert pattern.search("a").group(0) == "a"


def test_parallel_map_empty_subjects() -> None:
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    assert pcre.parallel_map(pattern, []) == []


def test_parallel_map_single_subject_avoids_executor(monkeypatch: pytest.MonkeyPatch) -> None:
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    monkeypatch.setattr(
        pcre_mod,
        "ensure_thread_pool",
        lambda *_args, **_kwargs: pytest.fail("single-item map must not create a pool"),
    )
    results = pcre.parallel_map(pattern, ["123"], method="findall")
    assert results == [["123"]]
    small_batch = pcre.parallel_map(pattern, ["1", "22", "333"], method="findall")
    assert small_batch == [["1"], ["22"], ["333"]]


def test_parallel_map_invalid_method() -> None:
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    with pytest.raises(ValueError, match="parallel_map"):
        pcre.parallel_map(pattern, ["1"], method="split")


def test_parallel_map_auto_threads_with_small_subjects(monkeypatch: pytest.MonkeyPatch) -> None:
    """When auto threshold is high, small subjects are processed sequentially."""
    monkeypatch.setattr(pcre_mod, "get_auto_threshold", lambda: 1_000_000)
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    results = pcre.parallel_map(pattern, ["1", "22", "333"])
    assert [m.group(0) for m in results] == ["1", "22", "333"]


def test_parallel_map_low_threshold_uses_threads(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_auto_threshold", lambda: 0)
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    results = pcre.parallel_map(pattern, ["1", "22", "333"])
    assert [m.group(0) for m in results] == ["1", "22", "333"]


def test_parallel_map_requires_thread_enabled() -> None:
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.NO_THREADS)
    with pytest.raises(RuntimeError, match="not enabled for threaded execution"):
        pcre.parallel_map(pattern, ["1"])


def test_parallel_map_memoryview_subject(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_auto_threshold", lambda: 0)
    pattern = pcre.compile(br"\d+", flags=pcre.Flag.THREADS)
    results = pcre.parallel_map(pattern, [memoryview(b"1"), memoryview(b"22")])
    assert [m.group(0) for m in results] == [b"1", b"22"]


def test_parallel_map_threading_unsupported_fallback(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "threading_supported", lambda: False)
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.THREADS)
    subjects = ["1", "22", "333", "4444", "5", "66", "777", "8888", "9"]
    results = pcre.parallel_map(pattern, subjects)
    assert [m.group(0) for m in results] == subjects


def test_compile_disabled_default_with_non_thread_flags(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: False)
    pattern = pcre.compile("a", flags=pcre.Flag.MULTILINE)
    assert pattern.thread_mode == pcre_mod._THREAD_MODE_DISABLED


def test_parallel_map_auto_mode_uses_threshold_zero(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_auto_threshold", lambda: 0)
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: True)
    pattern = pcre.compile(r"\d+")
    results = pcre.parallel_map(pattern, ["1", "22", "333"])
    assert [m.group(0) for m in results] == ["1", "22", "333"]


def test_parallel_map_auto_mode_with_memoryview_subject(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "get_auto_threshold", lambda: 1_000_000)
    monkeypatch.setattr(pcre_mod, "get_thread_default", lambda: True)
    pattern = pcre.compile(br"\d+")
    results = pcre.parallel_map(pattern, [memoryview(b"1"), memoryview(b"22")])
    assert [m.group(0) for m in results] == [b"1", b"22"]


def test_pattern_parallel_map_requires_thread_enabled() -> None:
    pattern = pcre.compile(r"\d+", flags=pcre.Flag.NO_THREADS)
    with pytest.raises(RuntimeError, match="not enabled for threaded execution"):
        pattern.parallel_map(["1"])


class _FakeRawMatch:
    def __init__(self, value: str, span: tuple[int, int], groups: tuple[str, ...] = ()) -> None:
        self._value = value
        self._span = span
        self._groups = groups

    def group(self, *indices: int) -> Any:
        if not indices:
            return self._value
        if len(indices) == 1:
            if indices[0] == 0:
                return self._value
            idx = indices[0] - 1
            return self._groups[idx] if 0 <= idx < len(self._groups) else None
        return tuple(self.group(i) for i in indices)

    def groups(self, default: Any = None) -> tuple[Any, ...]:
        return self._groups

    def span(self, group: int = 0) -> tuple[int, int]:
        return self._span

    def start(self, group: int = 0) -> int:
        return self._span[0]

    def end(self, group: int = 0) -> int:
        return self._span[1]


class _FakeCPattern:
    def __init__(self, *, findall: Any = None, finditer: Any = None) -> None:
        self.pattern = r"(a)(b?)"
        self.groupindex = {}
        self.flags = 0
        self.capture_count = 2
        self.jit = False
        self._findall = findall
        self._finditer = finditer
        self._call_count = 0

    def match(self, subject: Any, *, pos: int = 0, endpos: int = -1, options: int = 0) -> Any:
        if pos == 0 and subject.startswith("ab", pos):
            return _FakeRawMatch("ab", (0, 2), ("a", "b"))
        return None

    def search(self, subject: Any, *, pos: int = 0, endpos: int = -1, options: int = 0) -> Any:
        subject_str = subject.decode() if isinstance(subject, bytes) else subject
        length = len(subject_str)
        if pos < length:
            return _FakeRawMatch(subject_str[pos:pos + 1], (pos, pos + 1), (subject_str[pos:pos + 1],))
        if pos == length:
            # Zero-width match at the end, like re.finditer('').
            return _FakeRawMatch("", (pos, pos), ())
        return None

    def fullmatch(self, subject: Any, *, pos: int = 0, endpos: int = -1, options: int = 0) -> Any:
        if endpos == -1:
            endpos = len(subject)
        if pos == 0 and endpos == 1 and (subject == "a" or subject == b"a"):
            return _FakeRawMatch("a", (0, 1), ("a",))
        return None

    def findall(self, subject: Any, *, pos: int = 0, endpos: int = -1, options: int = 0) -> Any:
        if self._findall is not None:
            return self._findall(subject, pos=pos, endpos=endpos, options=options)
        raise TypeError("findall not implemented")

    def finditer(self, subject: Any, *, pos: int = 0, endpos: int = -1, options: int = 0) -> Any:
        if self._finditer is not None:
            return self._finditer(subject, pos=pos, endpos=endpos, options=options)
        raise TypeError("finditer not implemented")

    def split(self, subject: Any, maxsplit: int = 0) -> Any:
        raise TypeError("split not implemented")


def _fake_pattern(**kwargs: Any) -> pcre_mod.Pattern:
    return pcre_mod.Pattern(_FakeCPattern(**kwargs))


def test_finditer_uses_backend_iterator() -> None:
    raw_matches = [
        _FakeRawMatch("a", (0, 1), ("a",)),
        _FakeRawMatch("b", (1, 2), ("b",)),
    ]
    pattern = _fake_pattern(finditer=lambda subject, **kw: iter(raw_matches))
    matches = list(pattern.finditer("ab"))
    assert [m.group(0) for m in matches] == ["a", "b"]


def test_finditer_empty_backend_iterator() -> None:
    pattern = _fake_pattern(finditer=lambda subject, **kw: iter(()))
    assert list(pattern.finditer("ab")) == []


def test_finditer_owner_stamped_backend_iterator(monkeypatch: pytest.MonkeyPatch) -> None:
    pattern = _fake_pattern()
    raw = _FakeRawMatch("a", (0, 1), ("a",))
    raw.re = pattern
    pattern._pattern._finditer = lambda subject, **kw: iter((raw,))
    monkeypatch.setattr(pcre_mod, "_can_attach_match", lambda match: True)
    assert list(pattern.finditer("a")) == [raw]


def test_legacy_backend_match_no_match_paths() -> None:
    pattern = _fake_pattern()
    assert pattern.match("zz", pos=1) is None
    assert pattern.search("a") is not None
    assert pattern.search("", pos=1, endpos=0) is None
    assert pattern.fullmatch("zz") is None
    assert pattern.fullmatch("zz", endpos=1) is None


def test_pcre2_replacement_conversion_tuple_format() -> None:
    parsed_text = ([(1, 1)], ["$\\", None, "tail"])
    parsed_bytes = ([(1, 1)], [b"$\\", None, b"tail"])
    assert pcre_mod._pcre2_replacement_from_parsed(parsed_text, False) == (
        "$$" + "\\" * 3 + "g<1>tail"
    )
    assert pcre_mod._pcre2_replacement_from_parsed(parsed_bytes, True) == (
        b"$$" + b"\\" * 3 + b"g<1>tail"
    )


def test_findall_uses_backend_iterator() -> None:
    raw_matches = [
        _FakeRawMatch("a", (0, 1), ("a",)),
        _FakeRawMatch("b", (1, 2), ("b",)),
        _FakeRawMatch("", (2, 2), ()),
    ]
    pattern = _fake_pattern(finditer=lambda subject, **kw: iter(raw_matches))
    assert pattern.findall("ab") == ["a", "b", ""]


def test_finditer_fallback_loop_advances_zero_width_matches() -> None:
    pattern = _fake_pattern()
    matches = list(pattern.finditer("ab"))
    assert len(matches) == 3  # positions 0, 1, and a zero-width end match.


def test_finditer_fallback_loop_handles_zero_width_at_endpos() -> None:
    pattern = _fake_pattern()
    matches = list(pattern.finditer("ab", pos=2, endpos=2))
    assert len(matches) == 1
    assert list(pattern.finditer("ab", endpos=1))
    assert list(pattern.finditer("ab", pos=3)) == []


def test_findall_fallback_loop() -> None:
    pattern = _fake_pattern()
    assert pattern.findall("ab") == ["a", "b", ""]


def test_fullmatch_compat_match_path(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcre_mod, "_can_attach_match", lambda raw: False)
    pattern = pcre.compile(r"a")
    assert pattern.fullmatch("a").group(0) == "a"
    assert pattern.fullmatch("a", endpos=0) is None


def test_sub_renders_template_when_group_hint_missing() -> None:
    pattern = _fake_pattern()
    pattern._groups_hint = None
    result = pattern.sub(r"[\1]", "ab", count=2)
    assert result == "[a][b]"
    assert pattern._groups_hint == 1


def test_sub_raises_pcre_error_for_invalid_template() -> None:
    pattern = _fake_pattern()
    pattern._groups_hint = None
    with pytest.raises(pcre.PcreError):
        pattern.sub(r"[\2]", "ab")


def test_wrap_match_returns_none_for_none_raw() -> None:
    pattern = _fake_pattern()
    assert pattern._wrap_match(None, "x", 0, 1) is None
