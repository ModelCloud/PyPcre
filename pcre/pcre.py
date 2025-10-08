# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""High level operations for the :mod:`pcre` package."""

from __future__ import annotations

import re as _std_re
from collections.abc import Generator, Iterable
from re import _parser
from typing import Any, List

from . import cpcre2 as _pcre2
from .cache import cached_compile
from .cache import clear_cache as _clear_cache
from .flags import JIT, NO_JIT, NO_UCP, NO_UTF, strip_py_only_flags
from .re_compat import (
    Match,
    TemplatePatternStub,
    coerce_group_value,
    coerce_subject_slice,
    compute_next_pos,
    count_capturing_groups,
    is_bytes_like,
    join_parts,
    maybe_infer_group_count,
    normalise_count,
    normalise_replacement,
    prepare_subject,
    render_template,
    resolve_endpos,
)


_CPattern = _pcre2.Pattern
PcreError = _pcre2.PcreError

FlagInput = int | _std_re.RegexFlag | Iterable[int | _std_re.RegexFlag]

_DEFAULT_JIT = True


def _resolve_jit_setting(jit: bool | None) -> bool:
    if jit is None:
        return _DEFAULT_JIT
    return bool(jit)


def _extract_jit_override(flags: int) -> bool | None:
    override: bool | None = None
    if flags & JIT:
        override = True
    if flags & NO_JIT:
        if override is True:
            raise ValueError("Flag.JIT and Flag.NO_JIT cannot be combined")
        override = False
    return override


try:  # pragma: no cover - defensive fallback if backend lacks configure
    _DEFAULT_JIT = bool(_pcre2.configure())
except AttributeError:  # pragma: no cover - legacy backend without configure helper
    _DEFAULT_JIT = True

_STD_RE_FLAG_MAP: dict[_std_re.RegexFlag, int] = {
    _std_re.RegexFlag.IGNORECASE: _pcre2.PCRE2_CASELESS,
    _std_re.RegexFlag.MULTILINE: _pcre2.PCRE2_MULTILINE,
    _std_re.RegexFlag.DOTALL: _pcre2.PCRE2_DOTALL,
    _std_re.RegexFlag.VERBOSE: _pcre2.PCRE2_EXTENDED,
}

_STD_RE_FLAG_MASK = 0
for _flag in _STD_RE_FLAG_MAP:
    _STD_RE_FLAG_MASK |= int(_flag)


def _apply_default_unicode_flags(pattern: Any, flags: int) -> int:
    if not isinstance(pattern, str):
        return flags

    # Mirror stdlib `re` defaults: text patterns assume Unicode semantics unless
    # explicitly disabled via Flag.NO_UTF / Flag.NO_UCP.
    if flags & NO_UTF == 0 and flags & _pcre2.PCRE2_UTF == 0:
        flags |= _pcre2.PCRE2_UTF

    if flags & NO_UCP == 0 and flags & _pcre2.PCRE2_UCP == 0:
        flags |= _pcre2.PCRE2_UCP

    return flags


def _coerce_stdlib_regexflag(flag: _std_re.RegexFlag) -> int:
    unsupported_bits = int(flag) & ~_STD_RE_FLAG_MASK
    if unsupported_bits:
        unsupported = _std_re.RegexFlag(unsupported_bits)
        raise ValueError(
            f"Unsupported stdlib re flag {unsupported!r}: no equivalent PCRE option"
        )

    resolved = 0
    for std_flag, native_value in _STD_RE_FLAG_MAP.items():
        if flag & std_flag:
            resolved |= native_value
    return resolved


def _coerce_single_flag(flag: Any) -> int:
    if isinstance(flag, _std_re.RegexFlag):
        return _coerce_stdlib_regexflag(flag)
    if isinstance(flag, int):
        return int(flag)
    raise TypeError("flags must be ints, stdlib re flag values, or iterables thereof")


def _normalise_flags(flags: FlagInput) -> int:
    if isinstance(flags, _std_re.RegexFlag):
        return _coerce_stdlib_regexflag(flags)
    if isinstance(flags, int):
        return int(flags)
    if isinstance(flags, (str, bytes, bytearray)):
        raise TypeError("flags must be an int, stdlib re flag, or an iterable of those")
    if isinstance(flags, Iterable):
        resolved = 0
        for flag in flags:
            resolved |= _coerce_single_flag(flag)
        return resolved
    raise TypeError("flags must be an int, stdlib re flag, or an iterable of those")


def _call_with_optional_end(method, subject: Any, pos: int, endpos: int | None, options: int):
    resolved_end = resolve_endpos(subject, endpos)
    if endpos is None:
        return method(subject, pos=pos, options=options), resolved_end
    return method(subject, pos=pos, endpos=resolved_end, options=options), resolved_end


class Pattern:
    """High-level wrapper around the C-backed :class:`cpcre2.Pattern`."""

    __slots__ = ("_pattern", "_groups_hint")

    def __init__(self, pattern: _CPattern) -> None:
        self._pattern = pattern
        self._groups_hint = maybe_infer_group_count(pattern.pattern)

    def __repr__(self) -> str:  # pragma: no cover - delegated to C repr
        return repr(self._pattern)

    @property
    def pattern(self) -> Any:
        return self._pattern.pattern

    @property
    def groupindex(self) -> dict[str, int]:
        return self._pattern.groupindex

    @property
    def flags(self) -> int:
        return self._pattern.flags

    @property
    def jit(self) -> bool:
        return bool(self._pattern.jit)

    @property
    def groups(self) -> int:
        if self._groups_hint is None:
            self._groups_hint = count_capturing_groups(self.pattern)
        return self._groups_hint

    def _update_group_hint(self, match: Match) -> None:
        groups_count = len(match.groups())
        if self._groups_hint is None or groups_count > self._groups_hint:
            self._groups_hint = groups_count

    def _wrap_match(
        self,
        raw: Any,
        subject: Any,
        pos: int,
        end_boundary: int,
    ) -> Match | None:
        if raw is None:
            return None
        wrapped = Match(self, raw, subject, pos, end_boundary)
        self._update_group_hint(wrapped)
        return wrapped

    def match(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Match | None:
        subject = prepare_subject(subject)
        raw, resolved_end = _call_with_optional_end(self._pattern.match, subject, pos, endpos, options)
        return self._wrap_match(raw, subject, pos, resolved_end)

    def search(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Match | None:
        subject = prepare_subject(subject)
        raw, resolved_end = _call_with_optional_end(self._pattern.search, subject, pos, endpos, options)
        return self._wrap_match(raw, subject, pos, resolved_end)

    def fullmatch(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Match | None:
        subject = prepare_subject(subject)
        raw, resolved_end = _call_with_optional_end(self._pattern.fullmatch, subject, pos, endpos, options)
        return self._wrap_match(raw, subject, pos, resolved_end)

    def finditer(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Generator[Match, None, None]:
        subject = prepare_subject(subject)
        origin_pos = pos
        resolved_end = resolve_endpos(subject, endpos)
        search_end = resolved_end if endpos is not None else -1
        current = pos
        subject_length = len(subject)

        while True:
            raw = self._pattern.search(subject, pos=current, endpos=search_end, options=options)
            if raw is None:
                break

            match_obj = Match(self, raw, subject, origin_pos, resolved_end)
            self._update_group_hint(match_obj)
            yield match_obj

            start, end = match_obj.span()
            next_pos = compute_next_pos(current, (start, end), endpos)
            if next_pos <= current:
                next_pos = current + 1
            current = next_pos
            if current > subject_length:
                break
            if endpos is not None and current >= resolved_end:
                break

    def findall(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> List[Any]:
        results: List[Any] = []
        for match_obj in self.finditer(subject, pos=pos, endpos=endpos, options=options):
            groups = match_obj.groups()
            if groups:
                results.append(groups[0] if len(groups) == 1 else groups)
            else:
                results.append(match_obj.group(0))
        return results

    def split(self, subject: Any, maxsplit: Any = 0) -> List[Any]:
        subject = prepare_subject(subject)
        subject_is_bytes = is_bytes_like(subject)
        empty = b"" if subject_is_bytes else ""
        parts: List[Any] = []
        limit = normalise_count(maxsplit)

        last_end = 0
        splits_done = 0

        for match_obj in self.finditer(subject):
            if limit is not None and splits_done >= limit:
                break

            start, end = match_obj.span()
            parts.append(coerce_subject_slice(subject, last_end, start, is_bytes=subject_is_bytes))

            groups = match_obj.groups()
            if groups:
                for value in groups:
                    parts.append(coerce_group_value(value, is_bytes=subject_is_bytes, empty=empty))

            last_end = end
            splits_done += 1

        parts.append(coerce_subject_slice(subject, last_end, len(subject), is_bytes=subject_is_bytes))
        return parts

    def sub(self, repl: Any, subject: Any, count: Any = 0) -> Any:
        result, _ = self.subn(repl, subject, count)
        return result

    def subn(self, repl: Any, subject: Any, count: Any = 0) -> tuple[Any, int]:
        subject = prepare_subject(subject)
        subject_is_bytes = is_bytes_like(subject)
        empty = b"" if subject_is_bytes else ""
        limit = normalise_count(count)

        callable_repl = callable(repl)
        template = None
        parsed_template: List[Any] | None = None

        if not callable_repl:
            if subject_is_bytes:
                if not is_bytes_like(repl):
                    raise TypeError("replacement must be bytes-like when substituting on bytes")
                template = bytes(repl)
            else:
                if not isinstance(repl, str):
                    raise TypeError("replacement must be str when substituting on text")
                template = repl

            if self._groups_hint is not None:
                try:
                    parsed_template = _parser.parse_template(
                        template,
                        TemplatePatternStub(self._groups_hint, self.groupindex),
                    )
                except (ValueError, _std_re.error, IndexError) as exc:
                    raise PcreError(str(exc)) from exc

        parts: List[Any] = []
        substitutions = 0
        last_end = 0

        for match_obj in self.finditer(subject):
            if limit is not None and substitutions >= limit:
                break

            start, end = match_obj.span()
            parts.append(coerce_subject_slice(subject, last_end, start, is_bytes=subject_is_bytes))

            if not callable_repl:
                if parsed_template is None:
                    try:
                        parsed_template = _parser.parse_template(
                            template,
                            TemplatePatternStub(len(match_obj.groups()), self.groupindex),
                        )
                    except (ValueError, _std_re.error, IndexError) as exc:
                        raise PcreError(str(exc)) from exc
                    self._update_group_hint(match_obj)

                replacement = render_template(
                    parsed_template,
                    match_obj,
                    is_bytes=subject_is_bytes,
                    empty=empty,
                )
            else:
                replacement = normalise_replacement(repl(match_obj), is_bytes=subject_is_bytes)

            parts.append(replacement)

            substitutions += 1
            last_end = end

        parts.append(coerce_subject_slice(subject, last_end, len(subject), is_bytes=subject_is_bytes))
        result = join_parts(parts, is_bytes=subject_is_bytes)
        return result, substitutions


def compile(pattern: Any, flags: FlagInput = 0) -> Pattern:
    resolved_flags = _normalise_flags(flags)
    jit_override = _extract_jit_override(resolved_flags)
    resolved_jit = _resolve_jit_setting(jit_override)

    if isinstance(pattern, Pattern):
        if resolved_flags:
            raise ValueError("Cannot supply flags when using a Pattern instance.")
        if jit_override is not None and resolved_jit != pattern.jit:
            raise ValueError("Cannot override jit when using a Pattern instance.")
        return pattern

    if isinstance(pattern, _CPattern):
        if resolved_flags:
            raise ValueError("Cannot supply flags when using a compiled pattern instance.")
        if jit_override is not None:
            raise ValueError("Cannot supply jit when using a compiled pattern instance.")
        return Pattern(pattern)

    effective_flags = _apply_default_unicode_flags(pattern, resolved_flags)
    native_flags = strip_py_only_flags(effective_flags)

    return cached_compile(pattern, native_flags, Pattern, jit=resolved_jit)


def match(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).match(string)


def search(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).search(string)


def fullmatch(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).fullmatch(string)


def module_fullmatch(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    """Compat helper for code expecting a distinct module-level fullmatch."""

    return fullmatch(pattern, string, flags=flags)


def finditer(pattern: Any, string: Any, flags: FlagInput = 0) -> Iterable[Match]:
    return compile(pattern, flags=flags).finditer(string)


def findall(pattern: Any, string: Any, flags: FlagInput = 0) -> List[Any]:
    return compile(pattern, flags=flags).findall(string)


def split(pattern: Any, string: Any, maxsplit: Any = 0, flags: FlagInput = 0) -> List[Any]:
    return compile(pattern, flags=flags).split(string, maxsplit=maxsplit)


def sub(pattern: Any, repl: Any, string: Any, count: Any = 0, flags: FlagInput = 0) -> Any:
    return compile(pattern, flags=flags).sub(repl, string, count=count)


def subn(
    pattern: Any,
    repl: Any,
    string: Any,
    count: Any = 0,
    flags: FlagInput = 0,
) -> tuple[Any, int]:
    return compile(pattern, flags=flags).subn(repl, string, count=count)


def configure(*, jit: bool | None = None) -> bool:
    """Adjust global defaults for the high-level wrapper.

    Returns the effective default JIT setting after applying any updates.
    """

    global _DEFAULT_JIT

    if jit is None:
        try:
            _DEFAULT_JIT = bool(_pcre2.configure())
        except AttributeError:  # pragma: no cover - legacy backend without helper
            pass
        return _DEFAULT_JIT

    new_value = bool(jit)
    try:
        _DEFAULT_JIT = bool(_pcre2.configure(jit=new_value))
    except AttributeError:  # pragma: no cover - legacy backend without helper
        _DEFAULT_JIT = new_value
    return _DEFAULT_JIT


def clear_cache() -> None:
    """Clear the compiled pattern cache and release cached match-data buffers."""

    _clear_cache()
