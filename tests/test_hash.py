"""Unit tests for the adaptive sparse hash helper exposed by :mod:`pcre`."""
from __future__ import annotations

import ctypes
import random
import string
from typing import Iterable, Sequence

import pytest

from pcre import sparse_half_hash
import pcre_ext_c as _backend


# Hard-code the reference implementation so tests stay valid even if the C
# extension is unavailable and the Python fallback is in use.
FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3


def reference_sparse_hash(data: bytes | str) -> int:
    length = len(data)
    hash_value = FNV64_OFFSET

    stride = 2
    while length // stride > 8:
        stride <<= 1

    start = stride - 1
    if isinstance(data, str):
        for index in range(start, length, stride):
            hash_value ^= ord(data[index])
            hash_value = (hash_value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    else:
        view = memoryview(data)
        for index in range(start, length, stride):
            hash_value ^= view[index]
            hash_value = (hash_value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF

    hash_value ^= length >> 5
    result = ctypes.c_ssize_t(hash_value).value
    return -2 if result == -1 else result


def _random_ascii_strings(count: int, min_len: int, max_len: int, seed: int = 1337) -> list[str]:
    rng = random.Random(seed)
    alphabet = string.ascii_letters + string.digits
    return [
        "".join(rng.choices(alphabet, k=rng.randint(min_len, max_len)))
        for _ in range(count)
    ]


def _random_bytes(count: int, min_len: int, max_len: int, seed: int = 1234) -> list[bytes]:
    rng = random.Random(seed)
    return [rng.randbytes(rng.randint(min_len, max_len)) for _ in range(count)]


def _count_collisions(values: Iterable[int]) -> int:
    seen = set()
    collisions = 0
    for value in values:
        if value in seen:
            collisions += 1
        else:
            seen.add(value)
    return collisions


def _vary_even_positions(count: int, length: int, seed: int = 904) -> list[str]:
    base = list("abcdefghij" * ((length + 9) // 10))[:length]
    rng = random.Random(seed)
    out: list[str] = []
    for _ in range(count):
        sample = base[:]
        for index in range(0, length, 2):
            sample[index] = rng.choice(string.ascii_letters)
        out.append("".join(sample))
    return out


def _vary_odd_positions(count: int, length: int, seed: int = 905) -> list[str]:
    base = list("abcdefghij" * ((length + 9) // 10))[:length]
    rng = random.Random(seed)
    out: list[str] = []
    for _ in range(count):
        sample = base[:]
        for index in range(1, length, 2):
            sample[index] = rng.choice(string.ascii_letters)
        out.append("".join(sample))
    return out


def _suffix_counters(count: int, length: int) -> list[str]:
    width = max(length, 1)
    strings: list[str] = []
    for value in range(count):
        token = f"{value:0{width}d}"
        if len(token) < length:
            token = ("x" * (length - len(token))) + token
        elif len(token) > length:
            token = token[-length:]
        strings.append(token)
    return strings


def _cache_metrics(keys: Sequence[str]) -> tuple[int, float]:
    cache: dict[int, str] = {}
    collisions = 0
    for key in keys:
        h = sparse_half_hash(key)
        existing = cache.get(h)
        if existing is not None and existing != key:
            collisions += 1
        cache[h] = key

    retention = len(cache) / len(keys) if keys else 1.0
    return collisions, retention


@pytest.mark.parametrize("sample_count", [1000])
@pytest.mark.parametrize("min_len,max_len", [(1, 64), (32, 256)])
@pytest.mark.parametrize("payload_factory", [_random_ascii_strings, _random_bytes])
def test_sparse_hash_matches_reference(sample_count: int, min_len: int, max_len: int, payload_factory) -> None:
    payloads = payload_factory(sample_count, min_len, max_len)
    for item in payloads:
        expected = reference_sparse_hash(item)
        assert sparse_half_hash(item) == expected


def test_sparse_hash_low_collision_on_random_ascii() -> None:
    payloads = _random_ascii_strings(5000, 8, 256)
    collisions = _count_collisions(sparse_half_hash(item) for item in payloads)
    # With uniformly random ASCII input we expect virtually no collisions; allow a
    # small cushion in case of an unlucky sample.
    assert collisions <= 5


def test_sparse_hash_low_collision_on_random_bytes() -> None:
    payloads = _random_bytes(5000, 8, 256)
    collisions = _count_collisions(sparse_half_hash(item) for item in payloads)
    assert collisions <= 5


@pytest.mark.parametrize("length", [8, 16, 32, 64])
def test_sparse_hash_misses_even_position_variation(length: int) -> None:
    base = list("abcdefghij" * ((length + 9) // 10))[:length]
    rng = random.Random(904)
    samples = []
    for _ in range(256):
        s = base[:]
        for index in range(0, length, 2):  # mutate positions the sampler ignores
            s[index] = rng.choice(string.ascii_letters)
        samples.append("".join(s))

    hashes = [sparse_half_hash(item) for item in samples]
    assert _count_collisions(hashes) >= len(samples) - 5  # essentially all collide


@pytest.mark.parametrize("length", [8, 16, 32, 64])
def test_sparse_hash_tracks_odd_position_variation(length: int) -> None:
    base = list("abcdefghij" * ((length + 9) // 10))[:length]
    rng = random.Random(905)
    samples = []
    for _ in range(256):
        s = base[:]
        for index in range(1, length, 2):  # mutate sampled positions
            s[index] = rng.choice(string.ascii_letters)
        samples.append("".join(s))

    hashes = [sparse_half_hash(item) for item in samples]
    assert _count_collisions(hashes) == 0


@pytest.mark.parametrize("length", [16, 64, 256])
def test_sparse_hash_handles_bytes_like(length: int) -> None:
    payload = bytearray(range(255))[:length]
    result_bytes = sparse_half_hash(bytes(payload))
    result_bytearray = sparse_half_hash(payload)
    result_memoryview = sparse_half_hash(memoryview(payload))
    assert result_bytes == result_bytearray == result_memoryview


@pytest.mark.parametrize("length", [16, 32, 64])
def test_sparse_hash_collision_profiles(length: int) -> None:
    count = 2000

    random_keys = _random_ascii_strings(count, length, length)
    random_collisions, random_retention = _cache_metrics(random_keys)
    assert random_collisions <= 5
    assert random_retention >= 0.995

    odd_keys = _vary_odd_positions(count, length)
    odd_collisions, odd_retention = _cache_metrics(odd_keys)
    assert odd_collisions <= 5
    assert odd_retention >= 0.995

    even_keys = _vary_even_positions(count, length)
    even_collisions, even_retention = _cache_metrics(even_keys)
    assert even_collisions >= count - 5
    assert even_retention <= 0.01

    suffix_keys = _suffix_counters(count, length)
    suffix_collisions, suffix_retention = _cache_metrics(suffix_keys)
    assert suffix_collisions >= int(count * 0.9)
    assert suffix_retention <= 0.1


def test_sparse_cache_key_hash_and_equality() -> None:
    key_a = _backend.cache_key_get("pattern", 0, False)
    key_b = _backend.cache_key_get("pattern", 0, False)
    assert key_a == key_b
    assert hash(key_a) == hash(key_b)


def test_sparse_cache_key_dict_usage() -> None:
    cache = {}
    cache[_backend.cache_key_get("abc", 1, True)] = "value"
    assert cache[_backend.cache_key_get("abc", 1, True)] == "value"


def test_sparse_cache_key_handles_flag_collisions() -> None:
    cache = {}
    key_a = _backend.cache_key_get("abcd", 0, False)
    key_b = _backend.cache_key_get("abcd", 1, False)
    cache[key_a] = "A"
    cache[key_b] = "B"
    assert cache[_backend.cache_key_get("abcd", 0, False)] == "A"
    assert cache[_backend.cache_key_get("abcd", 1, False)] == "B"
