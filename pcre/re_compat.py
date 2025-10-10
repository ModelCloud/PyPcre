# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Compat helpers aligning the public API with :mod:`re`."""

from __future__ import annotations

import operator
import re as _std_re
from re import _parser
from typing import Any, List

import pcre_ext_c as _pcre2


_CRawMatch = _pcre2.Match


def prepare_subject(subject: Any) -> Any:
    if isinstance(subject, memoryview):
        return subject.tobytes()
    return subject


def is_bytes_like(value: Any) -> bool:
    return isinstance(value, (bytes, bytearray, memoryview))


def normalise_count(value: Any) -> int | None:
    if value is None:
        return None
    try:
        count = operator.index(value)
    except TypeError as exc:  # pragma: no cover - defensive
        raise exc
    if count <= 0:
        return None
    return count


def resolve_endpos(subject: Any, endpos: int | None) -> int:
    length = len(subject)
    if endpos is None or endpos < 0:
        return length
    return endpos


def compute_next_pos(current: int, span: tuple[int, int], endpos: int | None) -> int:
    start, end = span
    if endpos is not None and end >= endpos:
        return end
    if end == start:
        return end + 1
    return end


class TemplatePatternStub:
    __slots__ = ("groups", "groupindex")

    def __init__(self, groups: int, groupindex: dict[str, int]):
        self.groups = groups
        self.groupindex = groupindex


def coerce_group_value(value: Any, *, is_bytes: bool, empty: Any) -> Any:
    if value is None:
        return empty
    if is_bytes:
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        if isinstance(value, memoryview):
            return value.tobytes()
        raise TypeError("group reference must produce bytes-like result")
    if not isinstance(value, str):
        raise TypeError("group reference must produce str result")
    return value


def coerce_subject_slice(subject: Any, start: int, end: int, *, is_bytes: bool) -> Any:
    slice_value = subject[start:end]
    if is_bytes:
        if isinstance(slice_value, memoryview):
            return slice_value.tobytes()
        if isinstance(slice_value, bytearray):
            return bytes(slice_value)
        return slice_value if isinstance(slice_value, bytes) else bytes(slice_value)
    return slice_value


def normalise_replacement(value: Any, *, is_bytes: bool) -> Any:
    if is_bytes:
        if isinstance(value, (bytes, bytearray)):
            return bytes(value)
        if isinstance(value, memoryview):
            return value.tobytes()
        raise TypeError("replacement must be bytes-like when substituting on bytes")
    if not isinstance(value, str):
        raise TypeError("replacement must be str when substituting on text")
    return value


def join_parts(parts: List[Any], *, is_bytes: bool) -> Any:
    if is_bytes:
        normalised: List[bytes] = []
        for part in parts:
            if isinstance(part, bytes):
                normalised.append(part)
            elif isinstance(part, bytearray):
                normalised.append(bytes(part))
            elif isinstance(part, memoryview):
                normalised.append(part.tobytes())
            else:
                raise TypeError("bytes operations require bytes-like parts")
        return b"".join(normalised)
    return "".join(parts)


def render_template(parsed: Any, match: "Match", *, is_bytes: bool, empty: Any) -> Any:
    """Render a parsed replacement template across Python versions."""

    # Python 3.11's ``parse_template`` returns a ``(groups, literals)`` tuple,
    # whereas newer versions return a flat list. We support both shapes here.
    if (
        isinstance(parsed, tuple)
        and len(parsed) == 2
        and isinstance(parsed[0], list)
        and isinstance(parsed[1], list)
    ):
        group_slots, literals = parsed
        # Copy literals so repeated substitutions reuse the cached template.
        pieces: List[Any] = [empty if part is None else part for part in literals]
        for slot_index, group_index in group_slots:
            group_value = match.group(group_index)
            pieces[slot_index] = coerce_group_value(
                group_value,
                is_bytes=is_bytes,
                empty=empty,
            )
        return join_parts(pieces, is_bytes=is_bytes)

    pieces: List[Any] = []
    for item in parsed:
        if isinstance(item, int):
            group_value = match.group(item)
            pieces.append(coerce_group_value(group_value, is_bytes=is_bytes, empty=empty))
        else:
            pieces.append(item)
    return join_parts(pieces, is_bytes=is_bytes)


def maybe_infer_group_count(pattern_source: Any) -> int | None:
    normalised = pattern_source
    if isinstance(normalised, memoryview):
        normalised = normalised.tobytes()
    if isinstance(normalised, bytearray):
        normalised = bytes(normalised)

    try:
        compiled = _std_re.compile(normalised)
    except Exception:
        try:
            return count_capturing_groups(normalised)
        except Exception:  # pragma: no cover - defensive fallback
            return None
    return compiled.groups


def count_capturing_groups(pattern_source: Any) -> int:
    if isinstance(pattern_source, memoryview):
        pattern_source = pattern_source.tobytes()
    if isinstance(pattern_source, (bytes, bytearray)):
        text = pattern_source.decode("latin-1")
    else:
        text = str(pattern_source)

    length = len(text)
    count = 0
    i = 0
    in_class = False

    while i < length:
        char = text[i]
        if char == "\\":
            i += 2
            continue
        if char == "[":
            in_class = True
            i += 1
            continue
        if char == "]" and in_class:
            in_class = False
            i += 1
            continue
        if in_class:
            i += 1
            continue
        if char == "(":
            if is_capturing_group_start(text, i):
                count += 1
            i += 1
            continue
        i += 1

    return count


def is_capturing_group_start(source: str, index: int) -> bool:
    if source.startswith("(?P<", index) or source.startswith("(?P'", index):
        return True
    if source.startswith("(?<", index):
        return not (source.startswith("(?<=", index) or source.startswith("(?<!", index))
    if source.startswith("(?'", index):
        return True
    if source.startswith("(?|", index):
        return True
    if source.startswith("(?P=", index) or source.startswith("(?P>", index):
        return False
    if source.startswith("(?:", index):
        return False
    if source.startswith("(?>", index):
        return False
    if source.startswith("(?=", index) or source.startswith("(?!", index):
        return False
    if source.startswith("(?<=", index) or source.startswith("(?<!", index):
        return False
    if source.startswith("(?#", index):
        return False
    if source.startswith("(*", index):
        return False
    if source.startswith("(?", index):
        return False
    return True


class Match:
    __slots__ = ("_match", "_pattern", "_string", "_pos", "_endpos")

    def __init__(self, pattern: Any, match: _CRawMatch, subject: Any, pos: int, endpos: int) -> None:
        self._match = match
        self._pattern = pattern
        self._string = subject
        self._pos = pos
        self._endpos = endpos

    def __repr__(self) -> str:  # pragma: no cover - delegated to C repr
        return repr(self._match)

    def __getitem__(self, item: Any) -> Any:
        return self.group(item)

    def group(self, *indices: Any) -> Any:
        return self._match.group(*indices)

    def groups(self, default: Any = None) -> tuple[Any, ...]:
        return self._match.groups(default)

    def groupdict(self, default: Any = None) -> dict[str, Any]:
        return self._match.groupdict(default)

    def start(self, group: Any = 0) -> int:
        return self._match.start(group)

    def end(self, group: Any = 0) -> int:
        return self._match.end(group)

    def span(self, group: Any = 0) -> tuple[int, int]:
        return self._match.span(group)

    def expand(self, template: Any) -> Any:
        is_bytes = is_bytes_like(self._string)
        empty = b"" if is_bytes else ""
        if is_bytes:
            if not is_bytes_like(template):
                raise TypeError("template must be bytes-like for bytes matches")
            template = bytes(template)
        else:
            if not isinstance(template, str):
                raise TypeError("template must be str for text matches")

        parsed = _parser.parse_template(
            template,
            TemplatePatternStub(self.re.groups, self.re.groupindex),
        )
        return render_template(parsed, self, is_bytes=is_bytes, empty=empty)

    @property
    def re(self) -> Any:
        return self._pattern

    @property
    def string(self) -> Any:
        return self._string

    @property
    def pos(self) -> int:
        return self._pos

    @property
    def endpos(self) -> int:
        return self._endpos

    @property
    def lastindex(self) -> int | None:
        last: int | None = None
        for index, value in enumerate(self._match.groups(), start=1):
            if value is not None:
                last = index
        return last

    @property
    def lastgroup(self) -> str | None:
        index = self.lastindex
        if index is None:
            return None
        for name, group_index in self._pattern.groupindex.items():
            if group_index == index:
                return name
        return None

    @property
    def regs(self) -> tuple[tuple[int, int], ...]:
        group_count = len(self._match.groups())
        return tuple(self._match.span(i) for i in range(0, group_count + 1))


__all__ = [
    "Match",
    "TemplatePatternStub",
    "prepare_subject",
    "is_bytes_like",
    "normalise_count",
    "resolve_endpos",
    "compute_next_pos",
    "coerce_group_value",
    "coerce_subject_slice",
    "normalise_replacement",
    "join_parts",
    "render_template",
    "maybe_infer_group_count",
    "count_capturing_groups",
]
