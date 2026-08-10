import inspect
import re

import pytest

import pcre


@pytest.mark.parametrize(
    "value",
    [
        "",
        "identifier_123",
        "éclair世界",
        "()[]{}?*+-|^$\\.&~# \t\n\v\f\r",
        b"",
        b"identifier123",
        bytes(range(256)),
    ],
)
def test_escape_exact_builtins_match_stdlib(value):
    assert pcre.escape(value) == re.escape(value)


def test_escape_noop_exact_builtins_reuse_immutable_input():
    text = "identifier_123"
    data = b"identifier123"
    assert pcre.escape(text) is text
    assert pcre.escape(data) is data


@pytest.mark.parametrize("factory", [bytearray, memoryview])
def test_escape_bytes_like_fallback_matches_stdlib(factory):
    value = factory(b"a+b [c]")
    assert pcre.escape(value) == re.escape(value)


def test_escape_preserves_str_subclass_translate_override():
    class CustomString(str):
        def translate(self, table):
            assert isinstance(table, dict)
            return "custom-result"

    assert pcre.escape(CustomString("a+b")) == "custom-result"


def test_escape_bytes_subclass_matches_stdlib():
    class CustomBytes(bytes):
        pass

    value = CustomBytes(b"a+b")
    assert pcre.escape(value) == re.escape(value)
    assert type(pcre.escape(value)) is bytes


def test_escape_keyword_and_signature_match_stdlib():
    assert pcre.escape(pattern="a+b") == re.escape(pattern="a+b")
    assert inspect.signature(pcre.escape) == inspect.signature(re.escape)


@pytest.mark.parametrize(
    ("args", "kwargs"),
    [
        (("a", "b"), {}),
        (("a",), {"pattern": "b"}),
        ((), {}),
        ((), {"unexpected": "a"}),
    ],
)
def test_escape_argument_errors_match_stdlib(args, kwargs):
    with pytest.raises(TypeError) as expected:
        re.escape(*args, **kwargs)
    with pytest.raises(TypeError) as actual:
        pcre.escape(*args, **kwargs)
    assert str(actual.value) == str(expected.value)


@pytest.mark.parametrize("value", [None, 1, object()])
def test_escape_invalid_input_matches_stdlib_exception(value):
    with pytest.raises(TypeError):
        re.escape(value)
    with pytest.raises(TypeError):
        pcre.escape(value)
