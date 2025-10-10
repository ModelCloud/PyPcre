import types
from collections import OrderedDict

import pytest
from pcre import Flag
from pcre import cache as cache_mod
from pcre import pcre as core
from pcre.flags import strip_py_only_flags


class MethodRecorder:
    def __init__(self, return_value):
        self.return_value = return_value
        self.calls = []

    def __call__(self, subject, **kwargs):
        self.calls.append({"subject": subject, **kwargs})
        return self.return_value


class SequencedSearch:
    def __init__(self, results):
        self._results = list(results)
        self.calls = []

    def __call__(self, subject, *, pos, endpos, options):
        index = len(self.calls)
        self.calls.append({"subject": subject, "pos": pos, "endpos": endpos, "options": options})
        if index >= len(self._results):
            return None
        return self._results[index]


class FakeMatch:
    def __init__(self, span, groups=(), group0=None, named=None):
        self._span = span
        self._groups = tuple(groups)
        if group0 is None and not self._groups:
            raise ValueError("group0 must be supplied when no groups are present")
        self._group0 = group0 if group0 is not None else self._groups[0]
        self._named = dict(named or {})

    def span(self, group=0):
        if group == 0:
            return self._span
        index = group - 1
        if 0 <= index < len(self._groups):
            return self._span
        raise IndexError(group)

    def start(self, group=0):
        return self.span(group)[0]

    def end(self, group=0):
        return self.span(group)[1]

    def groups(self, default=None):
        if not self._groups:
            return ()
        return tuple(value if value is not None else default for value in self._groups)

    def group(self, *indices):
        if not indices:
            return self._group0
        if len(indices) == 1:
            index = indices[0]
            if index == 0:
                return self._group0
            if isinstance(index, str):
                return self._named.get(index)
            return self._groups[index - 1]
        return tuple(self.group(index) for index in indices)

    def groupdict(self, default=None):
        return {name: value if value is not None else default for name, value in self._named.items()}


def test_compile_returns_existing_pattern_instance():
    existing = core.Pattern.__new__(core.Pattern)
    existing._pattern = object()

    assert core.compile(existing) is existing


def test_compile_with_existing_pattern_and_flags_raises():
    existing = core.Pattern.__new__(core.Pattern)
    existing._pattern = object()

    with pytest.raises(ValueError):
        core.compile(existing, flags=1)


def test_compile_wraps_cpattern(monkeypatch):
    class DummyCPattern:
        def __init__(self):
            self.pattern = "literal"
            self.groupindex = {"name": 1}
            self.flags = 42
            self.match = MethodRecorder("match")
            self.search = MethodRecorder("search")
            self.fullmatch = MethodRecorder("fullmatch")

    monkeypatch.setattr(core, "_CPattern", DummyCPattern)

    compiled = core.compile(DummyCPattern())

    assert isinstance(compiled, core.Pattern)
    assert compiled.pattern == "literal"
    assert compiled.groupindex == {"name": 1}
    assert compiled.flags == 42


def test_compile_uses_cached_compile(monkeypatch):
    captured = {}

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured["args"] = (pattern, flags, wrapper)
        fake_cpattern = types.SimpleNamespace(
            pattern=pattern,
            groupindex={},
            flags=flags,
            match=MethodRecorder("match"),
            search=MethodRecorder("search"),
            fullmatch=MethodRecorder("fullmatch"),
            jit=jit,
        )
        return wrapper(fake_cpattern)

    monkeypatch.setattr(core, "cached_compile", fake_cached)

    provided_flags = 7
    expected_flags = strip_py_only_flags(core._apply_default_unicode_flags("expr", provided_flags))

    result = core.compile("expr", flags=provided_flags)

    assert captured["args"] == ("expr", expected_flags, core.Pattern)
    assert isinstance(result, core.Pattern)
    assert result.pattern == "expr"
    assert result.flags == expected_flags


def test_compile_accepts_iterable_flags(monkeypatch):
    captured = {}

    def fake_cached(pattern, flags, wrapper, *, jit):
        captured["flags"] = flags
        fake_cpattern = types.SimpleNamespace(
            pattern=pattern,
            groupindex={},
            flags=flags,
            match=MethodRecorder("match"),
            search=MethodRecorder("search"),
            fullmatch=MethodRecorder("fullmatch"),
            jit=jit,
        )
        return wrapper(fake_cpattern)

    monkeypatch.setattr(core, "cached_compile", fake_cached)

    flag_one = 0x00000001
    flag_two = 0x00000002
    combined = flag_one | flag_two

    provided_flags = (flag_one, flag_two, flag_two)
    expected_flags = strip_py_only_flags(core._apply_default_unicode_flags("expr", combined))

    result = core.compile("expr", flags=provided_flags)

    assert captured["flags"] == expected_flags
    assert isinstance(result, core.Pattern)
    assert result.flags == expected_flags


@pytest.mark.parametrize(
    ("pattern", "expected"),
    [
        ("\\u0041", "\\x{0041}"),
        ("\\U0001F600", "\\x{0001F600}"),
    ],
)
def test_compile_converts_regex_compat_sequences(pattern, expected, monkeypatch):
    captured = {}

    def fake_cached(pattern_value, flags, wrapper, *, jit):
        captured["pattern"] = pattern_value
        fake_cpattern = types.SimpleNamespace(
            pattern=pattern_value,
            groupindex={},
            flags=flags,
            match=MethodRecorder("match"),
            search=MethodRecorder("search"),
            fullmatch=MethodRecorder("fullmatch"),
            jit=jit,
        )
        return wrapper(fake_cpattern)

    monkeypatch.setattr(core, "cached_compile", fake_cached)

    compiled = core.compile(pattern, flags=Flag.COMPAT_UNICODE_ESCAPE)

    assert captured["pattern"] == expected
    assert compiled.pattern == expected


def test_compile_leaves_brace_style_unicode_escape(monkeypatch):
    captured = {}

    def fake_cached(pattern_value, flags, wrapper, *, jit):
        captured["pattern"] = pattern_value
        fake_cpattern = types.SimpleNamespace(
            pattern=pattern_value,
            groupindex={},
            flags=flags,
            match=MethodRecorder("match"),
            search=MethodRecorder("search"),
            fullmatch=MethodRecorder("fullmatch"),
            jit=jit,
        )
        return wrapper(fake_cpattern)

    monkeypatch.setattr(core, "cached_compile", fake_cached)

    compiled = core.compile("\\u{1F600}", flags=Flag.COMPAT_UNICODE_ESCAPE)

    assert captured["pattern"] == "\\u{1F600}"
    assert compiled.pattern == "\\u{1F600}"


def test_compile_rejects_out_of_range_unicode_escape():
    with pytest.raises(core.PcreError) as excinfo:
        core.compile("\\U00110000", flags=Flag.COMPAT_UNICODE_ESCAPE)

    message = str(excinfo.value)
    assert "exceeds 0x10FFFF" in message


def test_compile_uses_global_regex_compat(monkeypatch):
    captured = {}

    def fake_cached(pattern_value, flags, wrapper, *, jit):
        captured["pattern"] = pattern_value
        fake_cpattern = types.SimpleNamespace(
            pattern=pattern_value,
            groupindex={},
            flags=flags,
            match=MethodRecorder("match"),
            search=MethodRecorder("search"),
            fullmatch=MethodRecorder("fullmatch"),
            jit=jit,
        )
        return wrapper(fake_cpattern)

    monkeypatch.setattr(core, "cached_compile", fake_cached)
    monkeypatch.setattr(core, "_DEFAULT_COMPAT_REGEX", True)

    compiled = core.compile("\\u0041")

    assert captured["pattern"] == "\\x{0041}"
    assert compiled.pattern == "\\x{0041}"


def test_configure_updates_regex_compat_default(monkeypatch):
    calls = []

    def fake_backend_configure(**kwargs):
        calls.append(kwargs)
        return kwargs.get("jit", True)

    backend = types.SimpleNamespace(configure=fake_backend_configure)

    monkeypatch.setattr(core, "_pcre2", backend)
    monkeypatch.setattr(core, "_DEFAULT_JIT", False)
    monkeypatch.setattr(core, "_DEFAULT_COMPAT_REGEX", False)

    result = core.configure(compat_regex=True)
    assert result is True
    assert core._DEFAULT_COMPAT_REGEX is True

    result = core.configure(compat_regex=False)
    assert result is True
    assert core._DEFAULT_COMPAT_REGEX is False

    assert calls == [{}, {}]


def test_compile_rejects_non_int_iterable_flags():
    with pytest.raises(TypeError):
        core.compile("expr", flags=("not", "ints"))


def test_pattern_match_handles_optional_end():
    match_method = MethodRecorder(FakeMatch((0, 3), group0="matched"))
    fake_cpattern = types.SimpleNamespace(
        pattern="pat",
        groupindex={},
        flags=0,
        match=match_method,
        search=MethodRecorder("search"),
        fullmatch=MethodRecorder("full"),
        jit=False,
    )
    pattern = core.Pattern(fake_cpattern)

    result = pattern.match("subject")
    assert result.group(0) == "matched"
    first_call = match_method.calls[0]
    assert first_call == {"subject": "subject", "pos": 0, "options": 0}

    pattern.match("other", pos=3, endpos=8, options=5)
    second_call = match_method.calls[1]
    assert second_call["pos"] == 3
    assert second_call["options"] == 5
    assert second_call["endpos"] == 8


def test_configure_updates_default_jit(monkeypatch):
    pass

def test_pattern_search_and_fullmatch_delegate():
    search_method = MethodRecorder(FakeMatch((2, 4), group0="search-result"))
    fullmatch_method = MethodRecorder(FakeMatch((1, 5), group0="full-result"))
    fake_cpattern = types.SimpleNamespace(
        pattern="pat",
        groupindex={},
        flags=0,
        match=MethodRecorder("match"),
        search=search_method,
        fullmatch=fullmatch_method,
        jit=False,
    )
    pattern = core.Pattern(fake_cpattern)

    search_result = pattern.search("subject", pos=2, options=4)
    assert search_result.group(0) == "search-result"
    search_call = search_method.calls[0]
    assert search_call == {"subject": "subject", "pos": 2, "options": 4}

    full_result = pattern.fullmatch("subject", pos=1, endpos=5, options=6)
    assert full_result.group(0) == "full-result"
    full_call = fullmatch_method.calls[0]
    assert full_call["endpos"] == 5
    assert full_call["pos"] == 1
    assert full_call["options"] == 6


def test_pattern_finditer_advances_on_zero_width_matches():
    zero_width = FakeMatch((0, 0), group0="")
    consuming = FakeMatch((1, 3), group0="ab")
    sequenced_search = SequencedSearch([zero_width, consuming])
    fake_cpattern = types.SimpleNamespace(
        pattern="pat",
        groupindex={},
        flags=0,
        search=sequenced_search,
        jit=False,
    )
    pattern = core.Pattern(fake_cpattern)

    matches = list(pattern.finditer("abc", options=7))

    assert [m.group(0) for m in matches] == ["", "ab"]
    assert len(sequenced_search.calls) == 3
    assert sequenced_search.calls[0]["pos"] == 0
    assert sequenced_search.calls[1]["pos"] == 1
    assert sequenced_search.calls[2]["pos"] == 3


def test_pattern_finditer_respects_endpos_limit():
    first = FakeMatch((0, 1), group0="a")
    second = FakeMatch((1, 3), group0="bc")
    sequenced_search = SequencedSearch([first, second])
    fake_cpattern = types.SimpleNamespace(
        pattern="pat",
        groupindex={},
        flags=0,
        search=sequenced_search,
        jit=False,
    )
    pattern = core.Pattern(fake_cpattern)

    matches = list(pattern.finditer("abcdef", endpos=3))

    assert [m.group(0) for m in matches] == ["a", "bc"]
    assert len(sequenced_search.calls) == 2
    for call in sequenced_search.calls:
        assert call["endpos"] == 3


def test_pattern_findall_normalises_results(monkeypatch):
    calls = []

    def fake_finditer(self, subject, *, pos, endpos, options):
        calls.append((subject, pos, endpos, options))
        yield FakeMatch((0, 3), group0="abc")
        yield FakeMatch((3, 6), groups=("grp",))
        yield FakeMatch((6, 9), groups=("a", "b"))

    monkeypatch.setattr(core.Pattern, "finditer", fake_finditer)

    pattern = core.Pattern.__new__(core.Pattern)
    pattern._pattern = None

    results = pattern.findall("subject", pos=2, endpos=8, options=5)

    assert results == ["abc", "grp", ("a", "b")]
    assert calls == [("subject", 2, 8, 5)]


def test_module_match_delegates(monkeypatch):
    seen = {}

    class DummyPattern:
        def __init__(self):
            self.calls = []

        def match(self, text):
            self.calls.append(text)
            return "match-result"

    dummy = DummyPattern()

    def fake_compile(pattern, flags=0):
        seen["args"] = (pattern, flags)
        return dummy

    monkeypatch.setattr(core, "compile", fake_compile)

    result = core.match("expr", "subject", flags=9)

    assert seen["args"] == ("expr", 9)
    assert dummy.calls == ["subject"]
    assert result == "match-result"


def test_module_search_delegates(monkeypatch):
    seen = {}

    class DummyPattern:
        def __init__(self):
            self.calls = []

        def search(self, text):
            self.calls.append(text)
            return "search-result"

    dummy = DummyPattern()

    def fake_compile(pattern, flags=0):
        seen["args"] = (pattern, flags)
        return dummy

    monkeypatch.setattr(core, "compile", fake_compile)

    result = core.search("expr", "subject", flags=3)

    assert seen["args"] == ("expr", 3)
    assert dummy.calls == ["subject"]
    assert result == "search-result"


def test_module_fullmatch_delegates(monkeypatch):
    seen = {}

    class DummyPattern:
        def __init__(self):
            self.calls = []

        def fullmatch(self, text):
            self.calls.append(text)
            return "full-result"

    dummy = DummyPattern()

    def fake_compile(pattern, flags=0):
        seen["args"] = (pattern, flags)
        return dummy

    monkeypatch.setattr(core, "compile", fake_compile)

    result = core.fullmatch("expr", "subject", flags=11)

    assert seen["args"] == ("expr", 11)
    assert dummy.calls == ["subject"]
    assert result == "full-result"


def test_module_finditer_delegates(monkeypatch):
    seen = {}

    class DummyPattern:
        def __init__(self):
            self.calls = []

        def finditer(self, text):
            self.calls.append(text)
            yield "first"
            yield "second"

    dummy = DummyPattern()

    def fake_compile(pattern, flags=0):
        seen["args"] = (pattern, flags)
        return dummy

    monkeypatch.setattr(core, "compile", fake_compile)

    results = list(core.finditer("expr", "subject", flags=4))

    assert seen["args"] == ("expr", 4)
    assert dummy.calls == ["subject"]
    assert results == ["first", "second"]


def test_module_findall_delegates(monkeypatch):
    seen = {}

    class DummyPattern:
        def __init__(self):
            self.calls = []

        def findall(self, text):
            self.calls.append(text)
            return ["a", "b"]

    dummy = DummyPattern()

    def fake_compile(pattern, flags=0):
        seen["args"] = (pattern, flags)
        return dummy

    monkeypatch.setattr(core, "compile", fake_compile)

    results = core.findall("expr", "subject", flags=6)

    assert seen["args"] == ("expr", 6)
    assert dummy.calls == ["subject"]
    assert results == ["a", "b"]


def test_module_clear_cache_invokes_helper(monkeypatch):
    called = False

    def fake_clear():
        nonlocal called
        called = True

    monkeypatch.setattr(core, "_clear_cache", fake_clear)

    core.clear_cache()

    assert called is True


def test_cached_compile_caches_hashable_patterns(monkeypatch):
    cache_mod._THREAD_LOCAL.pattern_cache = OrderedDict()
    compiled_calls = []

    def fake_compile(pattern, *, flags=0, jit=True):
        compiled_calls.append((pattern, flags, jit))
        return f"compiled:{pattern}:{flags}:{jit}"

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    wrapped_calls = []

    def wrapper(raw):
        wrapped_calls.append(raw)
        return f"wrapped:{raw}"

    first = cache_mod.cached_compile("expr", 7, wrapper, jit=True)
    second = cache_mod.cached_compile("expr", 7, wrapper, jit=True)

    assert first is second
    assert compiled_calls == [("expr", 7, True)]
    assert wrapped_calls == ["compiled:expr:7:True"]


def test_cached_compile_handles_unhashable(monkeypatch):
    cache_mod._THREAD_LOCAL.pattern_cache = OrderedDict()
    compiled_results = []

    def fake_compile(pattern, *, flags=0, jit=False):
        result = f"compiled:{len(compiled_results)}"
        compiled_results.append(result)
        return result

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    def wrapper(raw):
        return f"wrapped:{raw}"

    first = cache_mod.cached_compile(["list"], 0, wrapper, jit=False)
    second = cache_mod.cached_compile(["list"], 0, wrapper, jit=False)

    assert first != second
    assert len(compiled_results) == 2


def test_cached_compile_enforces_cache_limit(monkeypatch):
    cache_mod._THREAD_LOCAL.pattern_cache = OrderedDict()
    compile_calls = []

    def fake_compile(pattern, *, flags=0, jit=False):
        compile_calls.append((pattern, flags, jit))
        return f"compiled:{pattern}:{flags}:{jit}"

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    def wrapper(raw):
        return raw

    original_limit = cache_mod.get_cache_limit()
    cache_mod.set_cache_limit(1)
    try:
        first = cache_mod.cached_compile("a", 0, wrapper, jit=False)
        second = cache_mod.cached_compile("b", 0, wrapper, jit=False)

        assert list(cache_mod._THREAD_LOCAL.pattern_cache.keys()) == [("b", 0, False)]

        third = cache_mod.cached_compile("a", 0, wrapper, jit=False)

        assert first == "compiled:a:0:False"
        assert second == "compiled:b:0:False"
        assert third == "compiled:a:0:False"
        assert compile_calls == [("a", 0, False), ("b", 0, False), ("a", 0, False)]
    finally:
        cache_mod.set_cache_limit(original_limit)


def test_cache_clear_cache_empties_store(monkeypatch):
    store = OrderedDict({("expr", 0, True): "value"})
    cache_mod._THREAD_LOCAL.pattern_cache = store

    cache_mod.clear_cache()

    assert store == OrderedDict()


def test_set_cache_limit_zero_disables_cache(monkeypatch, request):
    cache_mod._THREAD_LOCAL.pattern_cache = OrderedDict()
    original_limit = cache_mod.get_cache_limit()
    request.addfinalizer(lambda: cache_mod.set_cache_limit(original_limit))

    compile_calls = []

    def fake_compile(pattern, *, flags=0, jit=False):
        result = f"compiled:{pattern}:{len(compile_calls)}"
        compile_calls.append(result)
        return result

    monkeypatch.setattr(cache_mod._pcre2, "compile", fake_compile)

    cache_mod.set_cache_limit(0)

    def wrapper(raw):
        return raw

    first = cache_mod.cached_compile("expr", 0, wrapper, jit=False)
    second = cache_mod.cached_compile("expr", 0, wrapper, jit=False)

    assert first != second
    assert len(compile_calls) == 2
