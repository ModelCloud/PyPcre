import pcre
import pytest
from pcre import Flag


@pytest.fixture(autouse=True)
def reset_pcre_state():
    pcre.clear_cache()
    yield
    pcre.clear_cache()


def test_module_compile_and_match_shortcuts():
    compiled = pcre.compile(r"foo", Flag.CASELESS)
    assert isinstance(compiled, pcre.Pattern)
    assert compiled.match("FOO")

    match = pcre.match(r"foo", "FOO", flags=Flag.CASELESS)
    assert match is not None
    assert match.group(0) == "FOO"

    search = pcre.search(r"bar", "foo\nbar", flags=Flag.MULTILINE)
    assert search is not None
    assert search.group(0) == "bar"

    whole = pcre.fullmatch(r"\d+", "12345")
    assert whole is not None
    assert whole.span() == (0, 5)


def test_module_finditer_and_findall_helpers():
    matches = [m.group(0) for m in pcre.finditer(r"[A-Z]+", "abc DEF ghi JKL")]
    assert matches == ["DEF", "JKL"]

    groups = pcre.findall(r"(\w)(\d)", "a1 b2 c3")
    assert groups == [("a", "1"), ("b", "2"), ("c", "3")]


def test_module_split_and_substitutions():
    assert pcre.split(r"\s*,\s*", "a, b, c") == ["a", "b", "c"]
    assert pcre.split(r"\s+", "one   two", maxsplit=1) == ["one", "two"]

    templated = pcre.sub(r"(?P<num>\d+)", r"<\g<num>>", "item1 item2")
    assert templated == "item<1> item<2>"

    def bump(match):
        return str(int(match.group(0)) + 1)

    updated, count = pcre.subn(r"\d", bump, "a1 b2 c3", count=2)
    assert updated == "a2 b3 c3"
    assert count == 2


def test_module_clear_cache_resets_cached_patterns():
    first = pcre.compile(r"cached")
    second = pcre.compile(r"cached")
    assert first is second

    pcre.clear_cache()
    third = pcre.compile(r"cached")
    assert third is not first


def test_module_configure_roundtrip():
    original = pcre.configure()
    try:
        toggled = pcre.configure(jit=not original)
        assert toggled == (not original)
        assert pcre.configure() == toggled
    finally:
        pcre.configure(jit=original)
