# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""High level operations for the :mod:`pcre` package."""

from __future__ import annotations

import re as _std_re
from collections.abc import Iterable, Iterator, Mapping
from typing import Any, List

import pcre_ext_c as _pcre2

from ._stdlib_re import RE_TEMPLATE, RE_TEMPLATE_FLAG, RE_UNICODE_FLAG, _parser
from .cache import cached_compile
from .cache import clear_cache as _clear_cache
from .flags import Flag, strip_py_only_flags


# Cache frequently used flag values as plain integers to avoid the overhead of
# IntFlag arithmetic in hot paths such as module-level search helpers.
COMPAT_UNICODE_ESCAPE: int = int(Flag.COMPAT_UNICODE_ESCAPE)
THREADS: int = int(Flag.THREADS)
NO_THREADS: int = int(Flag.NO_THREADS)
JIT: int = int(Flag.JIT)
NO_JIT: int = int(Flag.NO_JIT)
NO_UTF: int = int(Flag.NO_UTF)
NO_UCP: int = int(Flag.NO_UCP)
from .re_compat import (
    Match as _CompatMatch,
)
from .re_compat import (
    TemplatePatternStub,
    coerce_group_value,
    coerce_subject_slice,
    compute_next_pos,
    is_bytes_like,
    join_parts,
    maybe_infer_group_count,
    normalise_count,
    normalise_replacement,
    prepare_subject,
    render_template,
    resolve_endpos,
)
from .threads import (
    ensure_thread_pool,
    get_auto_threshold,
    get_thread_default,
    threading_supported,
)


_CPattern = _pcre2.Pattern
PcreError = _pcre2.PcreError
Match = getattr(_pcre2, "Match", _CompatMatch)
_ATTACH_MATCH = getattr(_pcre2, "_attach_match", None)
_RAW_MATCH_TYPE = getattr(_pcre2, "Match", None)

FlagInput = int | _std_re.RegexFlag | Iterable[int | _std_re.RegexFlag]

_DEFAULT_JIT = True
_DEFAULT_COMPAT_REGEX = False


_THREAD_MODE_DISABLED = "disabled"
_THREAD_MODE_ENABLED = "enabled"
_THREAD_MODE_AUTO = "auto"


def _can_attach_match(raw: Any) -> bool:
    return (
        _ATTACH_MATCH is not None
        and _RAW_MATCH_TYPE is not None
        and isinstance(raw, _RAW_MATCH_TYPE)
    )


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


def _convert_regex_compat(pattern: str) -> str:
    return _pcre2.translate_unicode_escapes(pattern)


def _apply_regex_compat(pattern: Any, enabled: bool) -> Any:
    if not enabled or not isinstance(pattern, str):
        return pattern
    return _convert_regex_compat(pattern)


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
    unsupported_bits = int(flag) & ~(_STD_RE_FLAG_MASK | RE_TEMPLATE_FLAG | RE_UNICODE_FLAG)
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


def _pcre2_replacement_from_parsed(parsed: Any, is_bytes: bool) -> Any:
    """Convert a parsed Python replacement template to a PCRE2 replacement string."""

    if (
        isinstance(parsed, tuple)
        and len(parsed) == 2
        and isinstance(parsed[0], list)
        and isinstance(parsed[1], list)
    ):
        group_slots, literals = parsed
        slot_to_group = {slot: group for slot, group in group_slots}
        if is_bytes:
            parts = []
            for i, lit in enumerate(literals):
                if i in slot_to_group:
                    parts.append(("\\g<" + str(slot_to_group[i]) + ">").encode("ascii"))
                if lit is not None:
                    parts.append(lit.replace(b"\\", b"\\\\").replace(b"$", b"$$"))
            return b"".join(parts)

        parts = []
        for i, lit in enumerate(literals):
            if i in slot_to_group:
                parts.append("\\g<" + str(slot_to_group[i]) + ">")
            if lit is not None:
                parts.append(lit.replace("\\", "\\\\").replace("$", "$$"))
        return "".join(parts)

    if is_bytes:
        parts = []
        for item in parsed:
            if isinstance(item, int):
                parts.append(("\\g<" + str(item) + ">").encode("ascii"))
            else:
                parts.append(item.replace(b"\\", b"\\\\").replace(b"$", b"$$"))
        return b"".join(parts)

    parts = []
    for item in parsed:
        if isinstance(item, int):
            parts.append("\\g<" + str(item) + ">")
        else:
            parts.append(item.replace("\\", "\\\\").replace("$", "$$"))
    return "".join(parts)


class Pattern:
    """High-level wrapper around the C-backed :class:`pcre_ext_c.Pattern`."""

    __slots__ = ("_pattern", "_groups_hint", "_thread_mode", "_is_c_pattern")

    def __init__(self, pattern: _CPattern) -> None:
        self._pattern = pattern
        self._is_c_pattern = isinstance(pattern, _CPattern)
        self._thread_mode = _THREAD_MODE_DISABLED
        try:
            self._groups_hint = pattern.capture_count
        except AttributeError:  # pragma: no cover - older extension fallback
            self._groups_hint = maybe_infer_group_count(pattern.pattern)

    def __repr__(self) -> str:  # pragma: no cover - delegated to C repr
        return repr(self._pattern)

    @property
    def pattern(self) -> Any:
        return self._pattern.pattern

    @property
    def groupindex(self) -> Mapping[str, int]:
        return self._pattern.groupindex

    @property
    def flags(self) -> int:
        return self._pattern.flags

    @property
    def jit(self) -> bool:
        return bool(self._pattern.jit)

    @property
    def groups(self) -> int:
        return self._pattern.capture_count

    @property
    def thread_mode(self) -> str:
        return self._thread_mode

    @property
    def use_threads(self) -> bool:
        return self._thread_mode == _THREAD_MODE_ENABLED

    def enable_threads(self) -> None:
        self._thread_mode = _THREAD_MODE_ENABLED

    def disable_threads(self) -> None:
        self._thread_mode = _THREAD_MODE_DISABLED

    def enable_auto_threads(self) -> None:
        self._thread_mode = _THREAD_MODE_AUTO

    def _update_group_hint(self, match: Match) -> None:
        if self._groups_hint is not None:
            return
        groups_count = len(match.groups())
        if groups_count > 0:
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
        if _can_attach_match(raw):
            return _ATTACH_MATCH(raw, self)
        wrapped = _CompatMatch(self, raw, subject, pos, end_boundary)
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
        if type(subject) is memoryview:
            subject = subject.tobytes()
        if self._is_c_pattern:
            compiled_end = resolve_endpos(subject, endpos) if endpos is not None else -1
            return self._pattern.match(subject, pos, compiled_end, options, self)
        if endpos is None:
            resolved_end = len(subject)
            raw = self._pattern.match(subject, pos=pos, options=options)
        else:
            resolved_end = resolve_endpos(subject, endpos)
            raw = self._pattern.match(subject, pos=pos, endpos=resolved_end, options=options)
        if raw is None:
            return None
        return self._wrap_match(raw, subject, pos, resolved_end)

    prefixmatch = match

    def search(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Match | None:
        if type(subject) is memoryview:
            subject = subject.tobytes()
        if self._is_c_pattern:
            compiled_end = resolve_endpos(subject, endpos) if endpos is not None else -1
            return self._pattern.search(subject, pos, compiled_end, options, self)
        if endpos is None:
            resolved_end = len(subject)
            raw = self._pattern.search(subject, pos=pos, options=options)
        else:
            resolved_end = resolve_endpos(subject, endpos)
            raw = self._pattern.search(subject, pos=pos, endpos=resolved_end, options=options)
        if raw is None:
            return None
        return self._wrap_match(raw, subject, pos, resolved_end)

    def fullmatch(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Match | None:
        if type(subject) is memoryview:
            subject = subject.tobytes()
        if self._is_c_pattern:
            compiled_end = resolve_endpos(subject, endpos) if endpos is not None else -1
            return self._pattern.fullmatch(subject, pos, compiled_end, options, self)
        if endpos is None:
            resolved_end = len(subject)
            raw = self._pattern.fullmatch(subject, pos=pos, options=options)
        else:
            resolved_end = resolve_endpos(subject, endpos)
            raw = self._pattern.fullmatch(subject, pos=pos, endpos=resolved_end, options=options)
        if raw is None:
            return None
        return self._wrap_match(raw, subject, pos, resolved_end)

    def finditer(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> Iterator[Match]:
        if type(subject) is memoryview:
            subject = subject.tobytes()
        resolved_end = resolve_endpos(subject, endpos)
        backend_iter = getattr(self._pattern, "finditer", None)
        if backend_iter is not None:
            compiled_end = resolved_end if endpos is not None else -1
            if self._is_c_pattern:
                # The C iterator stamps each Match with this public Pattern, so
                # we can return it directly without per-match Python wrapping.
                return backend_iter(subject, pos, compiled_end, options, self)
            raw_iter = None
            try:
                raw_iter = backend_iter(subject, pos=pos, endpos=compiled_end, options=options, owner=self)
            except TypeError:
                # Older extensions and test doubles may not accept `owner`.
                try:
                    raw_iter = backend_iter(subject, pos=pos, endpos=compiled_end, options=options)
                except TypeError:
                    raw_iter = None
            if raw_iter is not None:
                # Peek to detect whether the backend already stamped the public
                # owner on its Match objects.  If it did, we can yield from the
                # raw iterator; otherwise wrap the raw results as before.
                try:
                    peek = next(raw_iter)
                except StopIteration:
                    return iter([])
                if _can_attach_match(peek) and peek.re is self:
                    def _owned_iter():
                        yield peek
                        yield from raw_iter
                    return _owned_iter()
                def _wrapped_iter():
                    yield self._wrap_match(peek, subject, pos, resolved_end)
                    for raw in raw_iter:
                        yield self._wrap_match(raw, subject, pos, resolved_end)
                return _wrapped_iter()

        search_end = resolved_end if endpos is not None else -1
        current = pos
        origin_pos = pos
        subject_length = len(subject)

        def _generator():
            nonlocal current
            while True:
                raw = self._pattern.search(subject, pos=current, endpos=search_end, options=options)
                if raw is None:
                    break

                match_obj = self._wrap_match(raw, subject, origin_pos, resolved_end)
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

        return _generator()

    def findall(
        self,
        subject: Any,
        *,
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
    ) -> List[Any]:
        if type(subject) is memoryview:
            subject = subject.tobytes()
        backend_findall = getattr(self._pattern, "findall", None)
        if backend_findall is not None:
            compiled_end = -1 if endpos is None else resolve_endpos(subject, endpos)
            if (
                self._is_c_pattern
                and type(pos) is int
                and pos == 0
                and endpos is None
                and type(options) is int
                and options == 0
            ):
                fast = getattr(self._pattern, "_findall_fast", None)
                if fast is not None:
                    return fast(subject)
            try:
                return backend_findall(subject, pos=pos, endpos=compiled_end, options=options)
            except TypeError:
                pass

        backend_iter = getattr(self._pattern, "finditer", None)
        if backend_iter is not None:
            compiled_end = -1 if endpos is None else resolve_endpos(subject, endpos)
            try:
                raw_iter = backend_iter(subject, pos=pos, endpos=compiled_end, options=options)
            except TypeError:
                raw_iter = None
            if raw_iter is not None:
                results: List[Any] = []
                for raw in raw_iter:
                    groups = raw.groups()
                    if groups:
                        results.append(groups[0] if len(groups) == 1 else groups)
                    else:
                        results.append(raw.group(0))
                return results

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
        limit = normalise_count(maxsplit)
        if limit == 0:
            subject_is_bytes = is_bytes_like(subject)
            return [
                coerce_subject_slice(
                    subject, 0, len(subject), is_bytes=subject_is_bytes
                )
            ]

        # Empty patterns split at every code point/byte; avoid per-match overhead.
        if limit is None and self.pattern in ("", b""):
            if is_bytes_like(subject):
                return [b""] + [bytes(subject[i : i + 1]) for i in range(len(subject))] + [b""]
            return [""] + list(subject) + [""]

        backend_split = getattr(self._pattern, "split", None)
        if backend_split is not None:
            try:
                return backend_split(subject, 0 if limit is None else limit)
            except TypeError:
                pass

        subject_is_bytes = is_bytes_like(subject)
        empty = b"" if subject_is_bytes else ""
        parts: List[Any] = []
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
        has_extended_syntax = False

        if not callable_repl:
            if subject_is_bytes:
                if not is_bytes_like(repl):
                    raise TypeError("replacement must be bytes-like when substituting on bytes")
                template = bytes(repl)
            else:
                if not isinstance(repl, str):
                    raise TypeError("replacement must be str when substituting on text")
                template = repl

            has_extended_syntax = (
                b"\\" in template or b"$" in template
                if subject_is_bytes
                else str.__contains__(template, "\\")
                or str.__contains__(template, "$")
            )

            if limit is None:
                backend_substitute = getattr(self._pattern, "substitute", None)
                if backend_substitute is not None:
                    # PCRE2's extended replacement syntax only treats ``\\``
                    # and ``$`` specially.  A replacement without either is
                    # already a literal Python template, so skip the costly
                    # ``sre_parse.parse_template`` round-trip.  This path is
                    # immutable and per-call, so it remains safe when the same
                    # Pattern is used concurrently by GIL-free threads.
                    if not has_extended_syntax:
                        try:
                            fast_substitute = getattr(self._pattern, "_substitute_fast", None)
                            if fast_substitute is not None:
                                return fast_substitute(subject, template)
                            return backend_substitute(subject, replacement=template, count=0)
                        except TypeError:
                            pass
                    try:
                        parsed_template = _parser.parse_template(
                            template,
                            TemplatePatternStub(self.groups, self.groupindex),
                        )
                    except (ValueError, _std_re.error, IndexError) as exc:
                        raise PcreError(str(exc)) from exc
                    pcre2_repl = _pcre2_replacement_from_parsed(parsed_template, subject_is_bytes)
                    try:
                        fast_substitute = getattr(self._pattern, "_substitute_fast", None)
                        if fast_substitute is not None:
                            return fast_substitute(subject, pcre2_repl)
                        backend_result = backend_substitute(subject, replacement=pcre2_repl, count=0)
                    except Exception:
                        backend_result = NotImplemented
                    if backend_result is not NotImplemented and backend_result is not None:
                        return backend_result
                    parsed_template = None

            if not has_extended_syntax:
                # A flat one-item parsed template is equivalent to a literal
                # replacement and avoids reparsing for bounded substitutions.
                parsed_template = [template]
            elif self._groups_hint is not None:
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


    def parallel_map(
        self,
        subjects: Iterable[Any],
        *,
        method: str = "search",
        pos: int = 0,
        endpos: int | None = None,
        options: int = 0,
        max_workers: int | None = None,
    ) -> List[Any]:
        if self._thread_mode == _THREAD_MODE_DISABLED:
            raise RuntimeError(
                "Pattern not enabled for threaded execution; compile with Flag.THREADS "
                "or configure threading defaults."
            )
        return parallel_map(
            self,
            subjects,
            method=method,
            pos=pos,
            endpos=endpos,
            options=options,
            max_workers=max_workers,
        )


def compile(pattern: Any, flags: FlagInput = 0) -> Pattern:
    # Fast path for the dominant shape: compile(pattern) with default flags.
    if flags == 0:
        if isinstance(pattern, Pattern):
            return pattern

        if isinstance(pattern, _CPattern):
            wrapper = Pattern(pattern)
            if get_thread_default():
                wrapper.enable_auto_threads()
            else:
                wrapper.disable_threads()
            return wrapper

        adjusted_pattern = _apply_regex_compat(pattern, bool(_DEFAULT_COMPAT_REGEX))
        if isinstance(adjusted_pattern, str):
            native_flags = _pcre2.PCRE2_UTF | _pcre2.PCRE2_UCP
        else:
            native_flags = 0
        compiled = cached_compile(adjusted_pattern, native_flags, Pattern, jit=_DEFAULT_JIT)
        if get_thread_default():
            compiled.enable_auto_threads()
        else:
            compiled.disable_threads()
        return compiled

    resolved_flags = _normalise_flags(flags)
    threads_requested = bool(resolved_flags & THREADS)
    no_threads_requested = bool(resolved_flags & NO_THREADS)
    compat_requested = bool(resolved_flags & COMPAT_UNICODE_ESCAPE)
    if threads_requested and no_threads_requested:
        raise ValueError("Flag.THREADS and Flag.NO_THREADS cannot be combined")

    resolved_flags_no_thread_markers = resolved_flags & ~(THREADS | NO_THREADS | COMPAT_UNICODE_ESCAPE)
    jit_override = _extract_jit_override(resolved_flags_no_thread_markers)
    resolved_jit = _resolve_jit_setting(jit_override)
    compat_enabled = bool(_DEFAULT_COMPAT_REGEX or compat_requested)

    if threads_requested:
        thread_mode = _THREAD_MODE_ENABLED
    elif no_threads_requested:
        thread_mode = _THREAD_MODE_DISABLED
    else:
        thread_mode = _THREAD_MODE_AUTO if get_thread_default() else _THREAD_MODE_DISABLED

    if isinstance(pattern, Pattern):
        if resolved_flags_no_thread_markers:
            raise ValueError("Cannot supply flags when using a Pattern instance.")
        if compat_requested:
            raise ValueError(
                "Cannot supply Flag.COMPAT_UNICODE_ESCAPE when using a Pattern instance."
            )
        if threads_requested:
            pattern.enable_threads()
        elif no_threads_requested:
            pattern.disable_threads()
        if jit_override is not None and resolved_jit != pattern.jit:
            raise ValueError("Cannot override jit when using a Pattern instance.")
        return pattern

    if isinstance(pattern, _CPattern):
        if resolved_flags_no_thread_markers:
            raise ValueError("Cannot supply flags when using a compiled pattern instance.")
        if jit_override is not None:
            raise ValueError("Cannot supply jit when using a compiled pattern instance.")
        if compat_requested:
            raise ValueError(
                "Cannot supply Flag.COMPAT_UNICODE_ESCAPE when using a compiled pattern instance."
            )
        wrapper = Pattern(pattern)
        if threads_requested:
            wrapper.enable_threads()
        elif no_threads_requested:
            wrapper.disable_threads()
        else:
            if thread_mode == _THREAD_MODE_AUTO:
                wrapper.enable_auto_threads()
            else:
                wrapper.disable_threads()
        return wrapper

    adjusted_pattern = _apply_regex_compat(pattern, compat_enabled)
    effective_flags = _apply_default_unicode_flags(
        adjusted_pattern, resolved_flags_no_thread_markers
    )
    native_flags = strip_py_only_flags(effective_flags)

    compiled = cached_compile(adjusted_pattern, native_flags, Pattern, jit=resolved_jit)
    if threads_requested:
        compiled.enable_threads()
    elif no_threads_requested:
        compiled.disable_threads()
    else:
        if thread_mode == _THREAD_MODE_AUTO:
            compiled.enable_auto_threads()
        else:
            compiled.disable_threads()
    return compiled


def prefixmatch(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).match(string)


match = prefixmatch


def search(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).search(string)


def fullmatch(pattern: Any, string: Any, flags: FlagInput = 0) -> Match | None:
    return compile(pattern, flags=flags).fullmatch(string)


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

# add this function to bypass signatures unit test
# re.template() is deprecated and removed since python 3.12
def template(pattern, flags=0):
    import warnings
    warnings.warn("The re.template() function is deprecated "
                  "as it is an undocumented function "
                  "without an obvious purpose. "
                  "Use re.compile() instead.",
                  DeprecationWarning)
    return compile(pattern, flags | RE_TEMPLATE)

_PARALLEL_EXEC_METHODS = frozenset({"match", "search", "fullmatch", "findall"})


def _subject_length(value: Any) -> int:
    if isinstance(value, (bytes, bytearray, memoryview)):
        return len(value)
    if isinstance(value, str):
        return len(value)
    raise TypeError(
        "parallel_map subjects must be str or bytes-like objects when auto threading "
        "is enabled"
    )


def _should_use_auto_threads(subjects: list[Any]) -> bool:
    threshold = get_auto_threshold()
    if threshold <= 0:
        return True
    max_length = 0
    for subject in subjects:
        length = _subject_length(subject)
        if length > max_length:
            max_length = length
            if max_length >= threshold:
                return True
    return False


def parallel_map(
    pattern: Any,
    subjects: Iterable[Any],
    *,
    method: str = "search",
    flags: FlagInput = 0,
    pos: int = 0,
    endpos: int | None = None,
    options: int = 0,
    max_workers: int | None = None,
) -> List[Any]:
    """Apply *method* across *subjects* using the shared PCRE thread pool.

    The order of *subjects* is preserved in the returned list. Supported executors are
    limited to stateless pattern lookups—``match``, ``search``, ``fullmatch``, and
    ``findall``—so that each task can run independently.
    """

    method_name = str(method)
    if method_name not in _PARALLEL_EXEC_METHODS:
        allowed = ", ".join(sorted(_PARALLEL_EXEC_METHODS))
        raise ValueError(f"parallel_map only supports {allowed} methods, got {method_name!r}")

    pattern_obj = compile(pattern, flags=flags)
    try:
        bound_method = getattr(pattern_obj, method_name)
    except AttributeError as exc:  # pragma: no cover - defensive guard
        raise ValueError(f"Pattern does not expose method {method_name!r}") from exc

    materials = list(subjects)
    if not materials:
        return []

    mode = pattern_obj.thread_mode
    if mode == _THREAD_MODE_DISABLED:
        raise RuntimeError(
            "Pattern not enabled for threaded execution; use Flag.THREADS or configure "
            "threading defaults."
        )

    if mode == _THREAD_MODE_AUTO and not _should_use_auto_threads(materials):
        return [
            bound_method(subject, pos=pos, endpos=endpos, options=options)
            for subject in materials
        ]

    if not threading_supported():
        return [
            bound_method(subject, pos=pos, endpos=endpos, options=options)
            for subject in materials
        ]

    executor = ensure_thread_pool(max_workers)
    futures = [
        executor.submit(bound_method, subject, pos=pos, endpos=endpos, options=options)
        for subject in materials
    ]
    return [future.result() for future in futures]


def configure(*, jit: bool | None = None, compat_regex: bool | None = None) -> bool:
    """Adjust global defaults for the high-level wrapper.

    Returns the effective default JIT setting after applying any updates. Supply
    ``compat_regex`` to change the default behaviour for :data:`Flag.COMPAT_UNICODE_ESCAPE`.
    """

    global _DEFAULT_JIT, _DEFAULT_COMPAT_REGEX

    if compat_regex is not None:
        _DEFAULT_COMPAT_REGEX = bool(compat_regex)

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
    """Clear the compiled pattern cache and release cached match-data/JIT buffers."""

    _clear_cache()
