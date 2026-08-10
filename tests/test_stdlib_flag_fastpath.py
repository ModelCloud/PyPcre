from __future__ import annotations

import concurrent.futures
import itertools
import re

import pcre_ext_c
import pytest

import pcre
from pcre import pcre as pcre_module

_SUPPORTED_STDLIB_FLAGS = tuple(
    flag
    for flag in (
        getattr(re.RegexFlag, "TEMPLATE", None),
        re.RegexFlag.IGNORECASE,
        re.RegexFlag.MULTILINE,
        re.RegexFlag.DOTALL,
        re.RegexFlag.UNICODE,
        re.RegexFlag.VERBOSE,
    )
    if flag is not None
)


@pytest.mark.parametrize("is_bytes", [False, True])
def test_every_supported_stdlib_regexflag_combination_is_exact(is_bytes: bool):
    source = b"a.b" if is_bytes else "a.b"
    default_flags = (
        0
        if is_bytes
        else (
            pcre_ext_c.PCRE2_UTF
            | pcre_ext_c.PCRE2_UCP
            | int(pcre.Flag.NEVER_BACKSLASH_C)
        )
    )
    native_by_stdlib = {
        re.RegexFlag.IGNORECASE: pcre_ext_c.PCRE2_CASELESS,
        re.RegexFlag.MULTILINE: pcre_ext_c.PCRE2_MULTILINE,
        re.RegexFlag.DOTALL: pcre_ext_c.PCRE2_DOTALL,
        re.RegexFlag.VERBOSE: pcre_ext_c.PCRE2_EXTENDED,
    }

    for enabled in itertools.product(
        (False, True), repeat=len(_SUPPORTED_STDLIB_FLAGS)
    ):
        flags = re.RegexFlag(0)
        expected = default_flags
        for include, stdlib_flag in zip(enabled, _SUPPORTED_STDLIB_FLAGS):
            if include:
                flags |= stdlib_flag
                expected |= native_by_stdlib.get(stdlib_flag, 0)

        assert pcre.compile(source, flags).flags == expected


@pytest.mark.parametrize(
    "unsupported",
    [
        re.RegexFlag.ASCII,
        re.RegexFlag.DEBUG,
        re.RegexFlag.LOCALE,
        re.RegexFlag.IGNORECASE | re.RegexFlag.ASCII,
        re.RegexFlag.MULTILINE | re.RegexFlag.DEBUG,
    ],
)
def test_direct_stdlib_regexflag_path_rejects_unsupported_bits(unsupported):
    with pytest.raises(ValueError, match="Unsupported stdlib re flag"):
        pcre.compile("a", unsupported)


def test_direct_stdlib_regexflag_path_uses_bounded_thread_local_cache():
    original_limit = pcre.get_cache_limit()
    try:
        pcre.set_cache_limit(2)
        for index in range(12):
            pcre.compile(f"pattern-{index}", re.RegexFlag.IGNORECASE)
        assert len(pcre_module._DEFAULT_COMPILE_LOCAL.flagged_cache) <= 2
    finally:
        pcre.set_cache_limit(original_limit)
        pcre.clear_cache()


def test_stdlib_regexflag_fast_path_is_safe_across_worker_threads():
    flags = re.RegexFlag.IGNORECASE | re.RegexFlag.MULTILINE | re.RegexFlag.DOTALL

    def exercise(index: int):
        pattern = pcre.compile(r"^a.(?P<tail>b)$", flags)
        match = pattern.search("ignored\nA\nb")
        return index, pattern.flags, None if match is None else match.group("tail")

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(exercise, range(256)))

    expected_native = (
        pcre_ext_c.PCRE2_UTF
        | pcre_ext_c.PCRE2_UCP
        | int(pcre.Flag.NEVER_BACKSLASH_C)
        | pcre_ext_c.PCRE2_CASELESS
        | pcre_ext_c.PCRE2_MULTILINE
        | pcre_ext_c.PCRE2_DOTALL
    )
    assert results == [(index, expected_native, "b") for index in range(256)]


def test_template_retains_deprecation_and_template_flag_semantics():
    with pytest.warns(DeprecationWarning, match="deprecated"):
        compiled = pcre.template(r"(?P<word>\w+)")
    assert compiled.fullmatch("hello") is not None
