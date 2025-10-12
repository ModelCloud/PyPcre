# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import os
import random
import string
import time
from typing import Iterable

import pcre
import pytest


# Exercise a broad cross-section of the API using randomized patterns/subjects.
_RUN_DURATION_SECONDS = 60.0

# Predefined building blocks that combine into (mostly) well-formed patterns.
_BASE_TOKENS: list[str] = [
    "a",
    "b",
    "c",
    "x",
    "y",
    "z",
    r"\w",
    r"\W",
    r"\d",
    r"\D",
    r"\s",
    r"\S",
    r"\b",
    r"\B",
    r"\A",
    r"\Z",
    ".",
    "[a-z]",
    "[A-Z]",
    "[0-9]",
    "[^aeiou]",
    "[\\w-]",
    "(?:ab)",
    "(?:cd|ef)",
    "(?:[a-z]{2})",
    "(?:\\d{2,4})",
    "(?:\\s+)",
    "(?#comment)",
    "(?=foo)",
    "(?!bar)",
    "(?<=baz)",
    "(?<!qux)",
    "(?P<word>\\w+)",
    "(?P<num>\\d+)",
    "(?:^|$)",
    "\\Qliteral\\E",
    "\\R",
]

# Intentional troublemakers that often yield syntax and runtime errors.
_BROKEN_TOKENS: list[str] = [
    "(",
    "[",
    "(?P<word>",
    "(?P=missing)",
    "(?42)",
    "\\",
    "(?<>)",
    "(?P<1>abc)",
    ")",
]

_ZERO_WIDTH_PREFIXES: tuple[str, ...] = (
    "(?=",
    "(?!",
    "(?<=",
    "(?<!",
    "(?#",
)

_INLINE_FLAG_WRAPPERS: tuple[str, ...] = (
    "(?i)",
    "(?m)",
    "(?s)",
    "(?x)",
    "(?u)",
    "(?J)",
    "(?i-m)",
)

_CANONICAL_FLAG_NAMES = tuple(
    name
    for name, flag in pcre.Flag.__members__.items()
    if getattr(flag, "name", name) == name
)

_TEXT_ONLY_FLAGS: frozenset[str] = frozenset({"COMPAT_UNICODE_ESCAPE"})

_FLAG_NAME_POOL: tuple[str, ...] = tuple(sorted(_CANONICAL_FLAG_NAMES))

_CONFLICTING_FLAGS: tuple[tuple[str, str], ...] = (
    ("JIT", "NO_JIT"),
    ("THREADS", "NO_THREADS"),
    ("UTF", "NO_UTF"),
    ("UTF", "NEVER_UTF"),
    ("UCP", "NO_UCP"),
    ("UCP", "NEVER_UCP"),
)


def _system_seed() -> int:
    return int.from_bytes(os.urandom(16), "little")


def _random_quantifier(rng: random.Random) -> str:
    bucket = rng.random()
    if bucket < 0.25:
        return ""
    if bucket < 0.4:
        return "?"
    if bucket < 0.55:
        return "+"
    if bucket < 0.7:
        return "*"
    if bucket < 0.85:
        return "{" + str(rng.randint(0, 4)) + "}"
    start = rng.randint(0, 3)
    end = rng.randint(start, start + 3)
    if rng.random() < 0.5:
        return f"{{{start},{end}}}"
    return f"{{{start},}}"


def _maybe_quantify(token: str, rng: random.Random) -> str:
    if (
        token in {"\\A", "\\Z", "\\b", "\\B", "(?:^|$)", "\\R"}
        or token.startswith(_ZERO_WIDTH_PREFIXES)
    ):
        return token
    quant = _random_quantifier(rng)
    return token + quant if quant else token


def _random_pattern(rng: random.Random) -> str:
    parts: list[str] = []
    length = rng.randint(1, 8)
    for _ in range(length):
        token_pool: Iterable[str] = _BASE_TOKENS
        if rng.random() < 0.15:
            token_pool = _BASE_TOKENS + _BROKEN_TOKENS
        token = rng.choice(tuple(token_pool))
        if rng.random() < 0.6:
            token = _maybe_quantify(token, rng)
        parts.append(token)
    pattern = "".join(parts)
    if pattern and rng.random() < 0.1:
        index = rng.randrange(len(pattern))
        pattern = pattern[:index] + pattern[index + 1 :]
    if rng.random() < 0.25:
        wrapper = rng.choice(_INLINE_FLAG_WRAPPERS)
        pattern = wrapper + pattern
    if rng.random() < 0.3:
        pattern = "(" + pattern + ")"
        if rng.random() < 0.6:
            quant = _random_quantifier(rng)
            if quant:
                pattern += quant
    if rng.random() < 0.2:
        extra_literal = chr(rng.randint(0x20, 0x2FFF))
        pattern += extra_literal
    return pattern


def _maybe_to_bytes(pattern: str, rng: random.Random) -> tuple[str | bytes, bool]:
    if rng.random() < 0.4:
        encoding = rng.choice(("utf-8", "latin-1"))
        data = pattern.encode(encoding, "ignore")
        if not data:
            return pattern, False
        if rng.random() < 0.1 and data:
            cut = rng.randrange(len(data))
            data = data[:cut] + data[cut + 1 :]
        return data, True
    return pattern, False


def _random_flags(rng: random.Random, is_bytes: bool) -> pcre.Flag:
    if not _FLAG_NAME_POOL:
        return pcre.Flag(0)
    selected: set[str] = set()
    flags = pcre.Flag(0)
    names = [
        name
        for name in _FLAG_NAME_POOL
        if name in pcre.Flag.__members__
        and (not is_bytes or name not in _TEXT_ONLY_FLAGS)
    ]
    rng.shuffle(names)
    for name in names:
        if not is_bytes and name in {"NO_UTF", "NO_UCP"}:
            continue
        if rng.random() < 0.35:
            if any(
                conflict for conflict in _CONFLICTING_FLAGS if name in conflict and selected.intersection(conflict)
            ):
                continue
            flags |= pcre.Flag.__members__[name]
            selected.add(name)
        if len(selected) >= 6:
            break
    return flags


def _random_subject(rng: random.Random, is_bytes: bool) -> str | bytes:
    length = rng.randint(0, 128)
    if is_bytes:
        alphabet = [0, 9, 10, 13, 27, 32, 33, 35, 36, 37, 38, 42, 43, 47, 58, 61, 64, 91, 93, 95, 97, 122, 255]
        return bytes(alphabet[rng.randrange(len(alphabet))] for _ in range(length))
    alphabet = string.ascii_letters + string.digits + string.punctuation + " \t\n\r" + "\u2603\u20AC\U0001F600"
    return "".join(rng.choice(alphabet) for _ in range(length))


def _random_replacement(rng: random.Random, is_bytes: bool, subject: str | bytes) -> str | bytes:
    if is_bytes:
        pool = [b"", b"X", b"Y", b"\\1", b"$1"]
        if subject:
            as_bytes = subject if isinstance(subject, bytes) else subject.encode("utf-8", "ignore")
            if as_bytes:
                span = rng.randint(1, min(len(as_bytes), 4))
                pool.append(as_bytes[:span])
                pool.append(as_bytes[-span:])
        return rng.choice(pool)
    pool_str = ["", "X", "Y", "\\1", "$1", "\\g<word>"]
    if subject:
        subject_str = subject if isinstance(subject, str) else subject.decode("utf-8", "ignore")
        if subject_str:
            span = rng.randint(1, min(len(subject_str), 4))
            pool_str.append(subject_str[:span])
            pool_str.append(subject_str[-span:])
    return rng.choice(pool_str)


def _exercise_pattern(
    compiled: pcre.Pattern,
    pattern: str | bytes,
    flags: pcre.Flag,
    subject: str | bytes,
    rng: random.Random,
) -> None:
    is_bytes = isinstance(subject, (bytes, bytearray))
    operations = (
        "match",
        "search",
        "fullmatch",
        "finditer",
        "findall",
        "split",
        "sub",
        "subn",
        "module_match",
        "module_search",
        "module_findall",
        "module_split",
        "module_sub",
        "module_subn",
    )
    for _ in range(rng.randint(1, 4)):
        op = rng.choice(operations)
        if op == "match":
            compiled.match(subject)
        elif op == "search":
            compiled.search(subject)
        elif op == "fullmatch":
            compiled.fullmatch(subject)
        elif op == "finditer":
            for _match in compiled.finditer(subject):
                _ = _match
        elif op == "findall":
            compiled.findall(subject)
        elif op == "split":
            compiled.split(subject, maxsplit=rng.randint(0, 4))
        elif op == "sub":
            compiled.sub(_random_replacement(rng, is_bytes, subject), subject, count=rng.randint(0, 3))
        elif op == "subn":
            compiled.subn(_random_replacement(rng, is_bytes, subject), subject, count=rng.randint(0, 3))
        elif op == "module_match":
            pcre.match(pattern, subject, flags=flags)
        elif op == "module_search":
            pcre.search(pattern, subject, flags=flags)
        elif op == "module_findall":
            pcre.findall(pattern, subject, flags=flags)
        elif op == "module_split":
            pcre.split(pattern, subject, maxsplit=rng.randint(0, 4), flags=flags)
        elif op == "module_sub":
            pcre.sub(pattern, _random_replacement(rng, is_bytes, subject), subject, count=rng.randint(0, 3), flags=flags)
        elif op == "module_subn":
            pcre.subn(pattern, _random_replacement(rng, is_bytes, subject), subject, count=rng.randint(0, 3), flags=flags)


def test_randomized_clobbering_ci_fuzz() -> None:
    seed = _system_seed()
    print(f'[test_clobber] seed={seed}')
    rng = random.Random(seed)
    deadline = time.monotonic() + _RUN_DURATION_SECONDS
    iterations = 0
    while time.monotonic() < deadline:
        iterations += 1
        pattern_str = _random_pattern(rng)
        pattern_input, is_bytes = _maybe_to_bytes(pattern_str, rng)
        flags = _random_flags(rng, is_bytes)

        try:
            compiled = pcre.compile(pattern_input, flags=flags)
        except pcre.error:
            continue
        except Exception as exc:  # pragma: no cover - unexpected failure should surface
            pytest.fail(
                f"compile error: seed={seed} pattern={pattern_input!r} flags={flags!r} error={exc!r}"
            )

        subject = _random_subject(rng, is_bytes)
        try:
            _exercise_pattern(compiled, pattern_input, flags, subject, rng)
        except pcre.error:
            continue
        except Exception as exc:  # pragma: no cover - unexpected failure should surface
            pytest.fail(
                "operation error: "
                f"seed={seed} pattern={pattern_input!r} subject_type={'bytes' if is_bytes else 'str'} "
                f"flags={flags!r} error={exc!r}"
            )

    assert iterations > 0
