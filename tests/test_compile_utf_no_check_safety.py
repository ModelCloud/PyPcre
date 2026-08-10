from __future__ import annotations

import concurrent.futures
import subprocess
import sys

import pcre_ext_c
import pytest

import pcre

_NO_UTF_CHECK = int(pcre.Flag.NO_UTF_CHECK)
_UTF_NO_CHECK = int(pcre.Flag.UTF | pcre.Flag.NO_UTF_CHECK)
_INVALID_UTF8_PATTERNS = (
    b"\xff",
    b"\xfe",
    b"\x80",
    b"\xc0\xaf",
    b"\xe2\x82",
    b"\xf0\x80\x80\x80",
    b"a\xed\xa0\x80z",
)


@pytest.mark.parametrize("pattern", _INVALID_UTF8_PATTERNS)
def test_invalid_utf_bytes_pattern_is_checked_despite_no_utf_check(pattern):
    with pytest.raises(pcre.PcreError):
        pcre_ext_c.compile(pattern, _UTF_NO_CHECK, jit=False)
    with pytest.raises(pcre.PcreError):
        pcre.compile(pattern, pcre.Flag.UTF | pcre.Flag.NO_UTF_CHECK)


@pytest.mark.parametrize("pattern", _INVALID_UTF8_PATTERNS)
def test_forced_validation_preserves_precise_pcre_error(pattern):
    with pytest.raises(pcre.PcreError) as checked:
        pcre_ext_c.compile(pattern, int(pcre.Flag.UTF), jit=False)
    with pytest.raises(pcre.PcreError) as guarded:
        pcre_ext_c.compile(pattern, _UTF_NO_CHECK, jit=False)
    assert guarded.value.code == checked.value.code
    assert guarded.value.offset == checked.value.offset


def test_valid_utf_bytes_pattern_preserves_requested_flag_and_behavior():
    source = "(?P<word>é+)".encode()
    pattern = pcre_ext_c.compile(source, _UTF_NO_CHECK, jit=False)
    match = pattern.fullmatch("éé".encode())
    assert match is not None
    assert match.group("word") == "éé".encode()
    assert pattern.flags & _NO_UTF_CHECK


def test_invalid_utf_no_check_compile_is_safe_in_subprocess():
    script = """
import pcre
import pcre_ext_c

flags = int(pcre.Flag.UTF | pcre.Flag.NO_UTF_CHECK)
for _ in range(2000):
    for pattern in (b"\\xff", b"\\x80", b"\\xe2\\x82", b"a\\xed\\xa0\\x80z"):
        try:
            pcre_ext_c.compile(pattern, flags, jit=bool(_ & 1))
        except pcre.PcreError:
            pass
        else:
            raise AssertionError(pattern)
"""
    completed = subprocess.run(
        [sys.executable, "-X", "faulthandler", "-c", script],
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr


def test_invalid_utf_no_check_compile_is_thread_safe():
    flags = pcre.Flag.UTF | pcre.Flag.NO_UTF_CHECK

    def exercise(worker: int):
        rejected = 0
        for index in range(1000):
            pattern = _INVALID_UTF8_PATTERNS[
                (worker + index) % len(_INVALID_UTF8_PATTERNS)
            ]
            try:
                pcre.compile(pattern, flags)
            except pcre.PcreError:
                rejected += 1
        return rejected

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        assert list(executor.map(exercise, range(8))) == [1000] * 8
