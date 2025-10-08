import pcre
from pcre import Flag


def test_pattern_match_and_properties():
    pattern_text = r"(?P<lead>\w+)-(\d+)"
    pattern = pcre.compile(pattern_text)

    assert pattern.pattern == pattern_text
    assert pattern.groupindex == {"lead": 1}
    assert pattern.groups == 2
    assert pattern.flags & Flag.UTF
    assert isinstance(pattern.jit, bool)

    subject = "alpha-123 omega"
    match = pattern.match(subject, endpos=9)
    assert match is not None
    assert match.group(0) == "alpha-123"
    assert match.group("lead") == "alpha"
    assert match.groups() == ("alpha", "123")
    assert match.span() == (0, 9)

    assert pattern.match("noop") is None


def test_pattern_search_and_fullmatch_behaviour():
    pattern = pcre.compile(r"(?P<word>\w+)-(\d+)")

    search_subject = "id=foo-42; foo-99"
    search = pattern.search(search_subject, pos=3, endpos=11)
    assert search is not None
    assert search.groupdict() == {"word": "foo"}
    assert search.span() == (3, 9)

    full_pattern = pcre.compile(r"\d{4}-\d{2}-\d{2}")
    full_match = full_pattern.fullmatch("2025-10-08")
    assert full_match is not None
    assert full_match.span() == (0, 10)


def test_pattern_finditer_and_findall_results():
    pattern = pcre.compile(r"(ab)(?:c)?")
    subject = "zzabcab"

    iterator = pattern.finditer(subject, pos=2)
    matches = [match.group(0) for match in iterator]
    assert matches == ["abc", "ab"]

    finder = pcre.compile(r"(\w)(\d)")
    assert finder.findall("a1 b2 c3") == [("a", "1"), ("b", "2"), ("c", "3")]


def test_pattern_split_with_capturing_groups_and_limit():
    pattern = pcre.compile(r"\s*(,)\s*")
    subject = "a, b, c"

    assert pattern.split(subject) == ["a", ",", "b", ",", "c"]
    assert pattern.split(subject, maxsplit=1) == ["a", ",", "b, c"]


def test_pattern_sub_and_subn_behaviour():
    pattern = pcre.compile(r"(?P<num>\d+)")
    subject = "item1 item2 item3"

    templated = pattern.sub(r"<\g<num>>", subject)
    assert templated == "item<1> item<2> item<3>"

    limited = pattern.sub(r"X", subject, count=1)
    assert limited == "itemX item2 item3"

    def replacer(match):
        return str(int(match.group("num")) * 2)

    substituted, count = pattern.subn(replacer, subject, count=2)
    assert substituted == "item2 item4 item3"
    assert count == 2
