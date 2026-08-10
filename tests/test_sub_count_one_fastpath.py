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


def test_count_two_still_uses_compatible_bounded_path():
    pattern = pcre.compile(r"(x)")
    assert pattern.sub(r"[\1]", "xxxx", count=2) == re.sub(
        r"(x)", r"[\1]", "xxxx", count=2
    )


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


def test_count_one_shared_pattern_is_thread_safe():
    pattern = pcre.compile(r"(?P<word>x)")

    def exercise(_: int):
        return pattern.subn(r"[\g<word>]", "xxxx", count=1)

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(exercise, range(256)))

    assert results == [("[x]xxx", 1)] * 256
