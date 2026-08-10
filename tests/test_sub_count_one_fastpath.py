import concurrent.futures
import re

import pytest

import pcre


@pytest.mark.parametrize(
    "pattern,replacement,subject",
    [
        (r"(x)", "[X]", "xxxx"),
        (r"(x)", r"[\1]", "xxxx"),
        (r"(x)", r"[\g<1>]", "xxxx"),
        (r"(?P<word>x)", r"[\g<word>]", "xxxx"),
        (rb"(x)", b"[X]", b"xxxx"),
        (rb"(x)", rb"[\1]", b"xxxx"),
        (rb"(x)", rb"[\g<1>]", b"xxxx"),
        (rb"(?P<word>x)", rb"[\g<word>]", b"xxxx"),
        (r"(x)", "replacement", "yyyy"),
        (r"", "-", "ab"),
    ],
)
def test_count_one_substitution_matches_stdlib(pattern, replacement, subject):
    expected = re.compile(pattern)
    actual = pcre.compile(pattern)

    assert actual.sub(replacement, subject, count=1) == expected.sub(
        replacement, subject, count=1
    )
    assert actual.subn(replacement, subject, count=1) == expected.subn(
        replacement, subject, count=1
    )
    assert pcre.sub(pattern, replacement, subject, count=1) == re.sub(
        pattern, replacement, subject, count=1
    )
    assert pcre.subn(pattern, replacement, subject, count=1) == re.subn(
        pattern, replacement, subject, count=1
    )


@pytest.mark.parametrize("count", [2, 4, 8])
def test_small_bounded_counts_use_compatible_native_path(count):
    pattern = pcre.compile(r"(x)")
    assert pattern.sub(r"[\1]", "x" * 12, count=count) == re.sub(
        r"(x)", r"[\1]", "x" * 12, count=count
    )


@pytest.mark.parametrize(
    "pattern,replacement,subject,count",
    [
        (r"(?=(.*))", r"[\1]", "abcd", 2),
        (r"(x)?", r"[\1]", "y", 4),
        (r"(?P<letter>é)", r"[\g<letter>]", "éééé", 8),
        (rb"(?=(.*))", rb"[\1]", b"abcd", 4),
    ],
)
def test_small_bounded_edge_cases_match_stdlib(pattern, replacement, subject, count):
    assert pcre.subn(pattern, replacement, subject, count=count) == re.subn(
        pattern, replacement, subject, count=count
    )


def test_count_nine_stays_on_compatible_python_path(monkeypatch):
    from pcre import pcre as pcre_module

    pattern = pcre.compile(r"(x)")
    original = pcre_module._cached_replacement_parts
    calls = 0

    def counted(*args):
        nonlocal calls
        calls += 1
        return original(*args)

    monkeypatch.setattr(pcre_module, "_cached_replacement_parts", counted)
    assert pattern.sub(r"[\1]", "x" * 12, count=9) == re.sub(
        r"(x)", r"[\1]", "x" * 12, count=9
    )
    assert calls == 1


def test_count_one_invalid_template_still_raises():
    pattern = pcre.compile(r"(x)")
    with pytest.raises(pcre.PcreError):
        pattern.sub(r"\q", "xxxx", count=1)


def test_count_one_preserves_duplicate_name_resolution():
    pattern = pcre.compile(
        r"(?J)(?<value>x)|(?<value>y)", pcre.Flag.DUPNAMES | pcre.Flag.NO_JIT
    )
    assert pattern.sub(r"[\g<value>]", "yy", count=1) == "[y]y"


def test_low_level_single_substitution_is_bounded():
    pattern = pcre.compile(r"(x)")._pattern
    assert pattern.substitute("xxxx", "[X]", 1) == ("[X]xxx", 1)
    assert pattern.substitute("yyyy", "[X]", 1) == ("yyyy", 0)
    assert pattern.substitute("xxxx", "[X]", 2) is NotImplemented


def test_bounded_reference_expansion_retries_with_linear_memory_bound():
    subject = "x" * 4096
    pattern = pcre.compile(r"(?=(.*))")
    expected = re.sub(r"(?=(.*))", r"[\1]", subject, count=8)
    assert pattern.sub(r"[\1]", subject, count=8) == expected


def test_bounded_callout_is_cleared_before_context_reuse():
    pattern = pcre.compile(r"(x)")
    assert pattern.sub(r"[\1]", "x" * 12, count=2) == "[x][x]" + "x" * 10
    assert pattern.sub(r"[\1]", "x" * 12) == "[x]" * 12
    assert pattern.sub(r"[\1]", "x" * 12, count=4) == "[x]" * 4 + "x" * 8


def test_small_bounded_references_do_not_grow_template_cache():
    from pcre import pcre as pcre_module

    pcre.clear_cache()
    pattern = pcre.compile(r"(x)")
    before = pcre_module._replacement_cache_size()
    for count in range(2, 9):
        assert pattern.sub(r"[\1]", "x" * 12, count=count) == re.sub(
            r"(x)", r"[\1]", "x" * 12, count=count
        )
    assert pcre_module._replacement_cache_size() == before


def test_count_one_shared_pattern_is_thread_safe():
    pattern = pcre.compile(r"(?P<word>x)")

    def exercise(_: int):
        return pattern.subn(r"[\g<word>]", "xxxx", count=1)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(exercise, range(256)))

    assert results == [("[x]xxx", 1)] * 256


def test_small_bounded_callout_is_thread_safe_on_shared_pattern():
    pattern = pcre.compile(r"(?P<word>x)")

    def exercise(index: int):
        count = 2 + index % 7
        return count, pattern.subn(r"[\g<word>]", "x" * 12, count=count)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(exercise, range(512)))

    for count, result in results:
        assert result == ("[x]" * count + "x" * (12 - count), count)
