# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import copy
import gc
import json
import os
import subprocess
import sys
import textwrap
import threading
from types import MappingProxyType

import pcre_ext_c as raw
import pytest

import pcre

TEXT_FLAGS = raw.PCRE2_UTF | raw.PCRE2_UCP


def test_match_unmatched_spans_and_atomic_protocols_match_re() -> None:
    match = pcre.compile(r"(?P<optional>a)?b").match("b")
    assert match is not None
    assert match.span(1) == (-1, -1)
    assert match.start(1) == -1
    assert match.end(1) == -1
    assert match[0] == "b"
    assert match["optional"] is None
    assert copy.copy(match) is match
    assert copy.deepcopy(match) is match


def test_match_repr_uses_character_offsets() -> None:
    match = pcre.search("é", "é")
    assert match is not None
    assert "span=(0, 1)" in repr(match)


def test_pattern_clamps_start_past_end_for_empty_match() -> None:
    pattern = raw.compile("", flags=TEXT_FLAGS, jit=False)
    for method_name in ("match", "search", "fullmatch"):
        match = getattr(pattern, method_name)("", 100)
        assert match is not None
        assert match.span() == (0, 0)


@pytest.mark.parametrize(
    "method_name,args",
    [
        ("match", (b"a",)),
        ("search", (b"a",)),
        ("fullmatch", (b"a",)),
        ("finditer", (b"a",)),
        ("findall", (b"a",)),
        ("split", (b"a",)),
        ("substitute", (b"a", b"x", 0)),
    ],
)
def test_text_pattern_rejects_bytes_subjects(
    method_name: str, args: tuple[object, ...]
) -> None:
    pattern = raw.compile("a", flags=TEXT_FLAGS, jit=False)
    with pytest.raises(TypeError, match="string pattern"):
        result = getattr(pattern, method_name)(*args)
        if method_name == "finditer":
            list(result)


@pytest.mark.parametrize(
    "method_name",
    ["match", "search", "fullmatch", "finditer", "findall", "split", "substitute"],
)
def test_bytes_pattern_rejects_text_subjects(method_name: str) -> None:
    pattern = raw.compile(b"a", jit=False)
    with pytest.raises(TypeError, match="bytes pattern"):
        args = ("a", "x", 0) if method_name == "substitute" else ("a",)
        result = getattr(pattern, method_name)(*args)
        if method_name == "finditer":
            list(result)


@pytest.mark.parametrize("method_name", ["match", "search", "fullmatch", "findall"])
@pytest.mark.parametrize("pattern,subject", [("a", b"a"), (b"a", "a")])
def test_module_execution_helpers_reject_mixed_types(
    method_name: str,
    pattern: str | bytes,
    subject: str | bytes,
) -> None:
    with pytest.raises(TypeError):
        getattr(raw, method_name)(pattern, subject, jit=False)


def test_utf_bytes_empty_matches_advance_by_codepoint() -> None:
    subject = "é".encode()
    pattern = raw.compile(b"", flags=TEXT_FLAGS, jit=False)
    assert [match.span() for match in pattern.finditer(subject)] == [(0, 0), (2, 2)]
    assert pattern.findall(subject) == [b"", b""]
    assert pattern.split(subject) == [b"", subject, b""]


def test_mutable_utf_buffer_is_snapshotted_before_iteration() -> None:
    subject = bytearray("é".encode())
    pattern = raw.compile(b"", flags=TEXT_FLAGS, jit=False)
    iterator = pattern.finditer(subject)
    subject[:] = b"\xc3\xff"
    assert [match.span() for match in iterator] == [(0, 0), (2, 2)]


def test_direct_split_negative_maxsplit_is_unsplit() -> None:
    pattern = raw.compile("a", flags=TEXT_FLAGS, jit=False)
    assert pattern.split("a-a", -1) == ["a-a"]


def test_groupindex_is_read_only_and_in_definition_order() -> None:
    pattern = pcre.compile(r"(?P<z>a)(?P<a>b)(?P<m>c)")
    assert isinstance(pattern.groupindex, MappingProxyType)
    assert list(pattern.groupindex) == ["z", "a", "m"]
    with pytest.raises(TypeError):
        pattern.groupindex["z"] = 99  # type: ignore[index]


@pytest.mark.parametrize("subject,expected", [("a", "a"), ("b", "b")])
def test_duplicate_name_selects_participating_capture(
    subject: str, expected: str
) -> None:
    pattern = pcre.compile(r"(?P<x>a)|(?P<x>b)", pcre.Flag.DUPNAMES)
    match = pattern.fullmatch(subject)
    assert match is not None
    assert pattern.groupindex == {"x": 1}
    assert match.group("x") == expected
    assert match.groupdict() == {"x": expected}


def test_global_inline_options_are_exposed() -> None:
    assert pcre.compile(r"(?i)a").flags & int(pcre.Flag.CASELESS)
    assert not (pcre.compile(r"(?i:a)").flags & int(pcre.Flag.CASELESS))


def test_text_backslash_c_is_rejected_before_matching_code_units() -> None:
    with pytest.raises(pcre.PcreError, match=r"\\C"):
        pcre.compile(r"\C", pcre.Flag.NO_JIT)


def test_uint32_flags_and_options_do_not_silently_wrap() -> None:
    with pytest.raises(OverflowError):
        raw.compile("a", flags=1 << 32)
    pattern = raw.compile("a", flags=TEXT_FLAGS, jit=False)
    for method_name in ("match", "search", "fullmatch", "finditer", "findall"):
        with pytest.raises(OverflowError):
            result = getattr(pattern, method_name)("a", options=1 << 32)
            if method_name == "finditer":
                list(result)


def test_explicit_jit_is_not_satisfied_by_default_jit_fallback_cache() -> None:
    original = raw.configure()
    raw.clear_pattern_cache()
    try:
        raw.configure(jit=True)
        fallback = raw.compile(b"\\C", flags=raw.PCRE2_UTF)
        if fallback.jit:
            pytest.skip("linked PCRE2 JIT supports UTF \\C")
        with pytest.raises(raw.PcreError):
            raw.compile(b"\\C", flags=raw.PCRE2_UTF, jit=True)
    finally:
        raw.configure(jit=original)
        raw.clear_pattern_cache()


def test_cache_configuration_rejects_negative_sizes() -> None:
    for setter in (raw.set_match_data_cache_size, raw.set_jit_stack_cache_size):
        with pytest.raises(OverflowError):
            setter(-1)
    with pytest.raises(OverflowError):
        raw.set_jit_stack_limits(-1, -1)


def test_global_cache_backend_strategy_and_reentrant_hash() -> None:
    source = textwrap.dedent(
        """
        import json
        import pcre_ext_c as raw

        class ReentrantPattern(str):
            def __hash__(self):
                raw.compile("inner", jit=False)
                return super().__hash__()

        compiled = raw.compile(ReentrantPattern("outer"), jit=False)
        print(json.dumps({"strategy": raw.get_cache_strategy(), "pattern": compiled.pattern}))
        """
    )
    env = os.environ.copy()
    env["PYPCRE_CACHE_PATTERN_GLOBAL"] = "1"
    completed = subprocess.run(
        [sys.executable, "-c", source],
        env=env,
        capture_output=True,
        text=True,
        timeout=10,
        check=True,
    )
    assert json.loads(completed.stdout) == {"strategy": "global", "pattern": "outer"}


def test_global_caches_are_consistent_during_concurrent_reconfiguration() -> None:
    source = textwrap.dedent(
        """
        import concurrent.futures
        import json
        import pcre_ext_c as raw

        patterns = [raw.compile("(" * n + "a" + ")" * n) for n in range(1, 17)]

        def match_worker(seed):
            for iteration in range(1_500):
                pattern = patterns[(iteration + seed) % len(patterns)]
                match = pattern.fullmatch("a")
                assert match is not None
                assert len(match.groups()) == pattern.capture_count

        def configure_worker():
            limits = ((32 * 1024, 1024 * 1024), (64 * 1024, 2 * 1024 * 1024))
            for iteration in range(750):
                raw.set_match_data_cache_size(iteration & 1)
                raw.set_jit_stack_cache_size(iteration & 1)
                raw.set_jit_stack_limits(*limits[iteration & 1])
                if iteration % 7 == 0:
                    raw.clear_match_data_cache()
                    raw.clear_jit_stack_cache()

        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            futures = [executor.submit(match_worker, index) for index in range(7)]
            futures.append(executor.submit(configure_worker))
            for future in futures:
                future.result()

        print(json.dumps({
            "strategy": raw.get_cache_strategy(),
            "limits": raw.get_jit_stack_limits(),
        }))
        """
    )
    env = os.environ.copy()
    env["PYPCRE_CACHE_PATTERN_GLOBAL"] = "1"
    completed = subprocess.run(
        [sys.executable, "-c", source],
        env=env,
        capture_output=True,
        text=True,
        timeout=30,
        check=True,
    )
    result = json.loads(completed.stdout)
    assert result["strategy"] == "global"
    assert result["limits"] in ([32 * 1024, 1024 * 1024], [64 * 1024, 2 * 1024 * 1024])


def test_thread_pattern_cache_releases_references_at_thread_exit() -> None:
    pattern = f"thread-exit-{id(object())}"
    baseline = sys.getrefcount(pattern)

    thread = threading.Thread(target=lambda: raw.compile(pattern, jit=False))
    thread.start()
    thread.join()
    del thread
    gc.collect()

    assert sys.getrefcount(pattern) == baseline


@pytest.mark.skipif(
    sys.version_info < (3, 14), reason="requires the 3.14 interpreter API"
)
def test_process_global_extension_is_rejected_in_subinterpreters() -> None:
    source = textwrap.dedent(
        """
        import _interpreters as interpreters
        import pcre_ext_c

        interpreter = interpreters.create()
        result = interpreters.exec(interpreter, "import pcre_ext_c")
        if result is None or result.type.__name__ != "ImportError":
            raise AssertionError("single-phase extension unexpectedly crossed interpreters")
        print("safely rejected")
        """
    )
    completed = subprocess.run(
        [sys.executable, "-c", source],
        capture_output=True,
        text=True,
        timeout=10,
        check=True,
    )
    assert completed.stdout.strip() == "safely rejected"


@pytest.mark.skipif(
    not getattr(sys, "_is_gil_enabled", lambda: True)(),
    reason="exercises the standard-GIL lock handoff",
)
def test_forced_jit_serialization_does_not_deadlock_with_gil_release() -> None:
    source = textwrap.dedent(
        """
        import concurrent.futures
        import threading
        import pcre_ext_c as raw

        pattern = raw.compile("a+$", jit=True)
        subject = "a" * 1_000_000
        barrier = threading.Barrier(2)

        def worker():
            barrier.wait()
            for _ in range(5):
                match = pattern.fullmatch(subject)
                assert match is not None

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            futures = [executor.submit(worker) for _ in range(2)]
            for future in futures:
                future.result()
        """
    )
    env = os.environ.copy()
    env["PYPCRE_FORCE_JIT_LOCK"] = "1"
    subprocess.run(
        [sys.executable, "-c", source],
        env=env,
        capture_output=True,
        text=True,
        timeout=10,
        check=True,
    )


def test_unicode_escape_translation_respects_escaping_and_comments() -> None:
    assert raw.translate_unicode_escapes(r"\u0041") == r"\x{0041}"
    assert raw.translate_unicode_escapes(r"\\u0041") == r"\\u0041"
    assert raw.translate_unicode_escapes(r"(?#\U00110000)a") == r"(?#\U00110000)a"
    assert raw.translate_unicode_escapes(r"\U") == r"\U"


def test_exported_high_bit_flags_are_unsigned() -> None:
    assert raw.PCRE2_ANCHORED == 0x80000000
