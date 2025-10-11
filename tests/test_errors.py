# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0

import pcre
import pcre_ext_c
import pytest


BACKEND = pcre_ext_c


def _error_class_by_macro() -> dict[str, type[pcre.PcreError]]:
    mapping: dict[str, type[pcre.PcreError]] = {}
    for name in dir(pcre):
        if name == "PcreError":
            continue
        if not (name.startswith("PcreError") or name.startswith("PyError")):
            continue
        candidate = getattr(pcre, name)
        macro = getattr(candidate, "macro", None)
        if not macro:
            continue
        mapping.setdefault(macro, candidate)
        if name.startswith("PcreError"):
            mapping[macro] = candidate
    return mapping


def test_error_enum_matches_exported_classes():
    mapping = _error_class_by_macro()
    assert mapping, "expected error subclasses to be exported from pcre"

    for member in pcre.PcreErrorCode:
        macro = f"PCRE2_ERROR_{member.name}"
        assert macro in mapping, f"missing {macro} subclass for {member}"
        exc_type = mapping[macro]
        assert issubclass(exc_type, pcre.PcreError)
        assert getattr(exc_type, "code") == member.value


_COMPILE_CASES: list[tuple[str, type[pcre.PcreError]]] = []
if hasattr(pcre, "PcreErrorMissingClosingParenthesis"):
    _COMPILE_CASES.append(("(", pcre.PcreErrorMissingClosingParenthesis))
if hasattr(pcre, "PcreErrorDuplicateSubpatternName"):
    _COMPILE_CASES.append(("(?P<dup>a)(?P<dup>b)", pcre.PcreErrorDuplicateSubpatternName))


@pytest.mark.parametrize("pattern,exc_type", _COMPILE_CASES, ids=[case[1].__name__ for case in _COMPILE_CASES])
def test_compile_errors_raise_specific_subclasses(pattern: str, exc_type: type[pcre.PcreError]):
    if not _COMPILE_CASES:
        pytest.skip("PCRE2 build exposes no compile-time error subclasses")

    with pytest.raises(exc_type) as info:
        pcre.compile(pattern)

    exc = info.value
    assert exc.macro == exc_type.macro
    assert exc.code == exc_type.code
    enum_member = pcre.PcreErrorCode[exc_type.macro.removeprefix("PCRE2_ERROR_")]
    assert exc.error_code is enum_member


@pytest.mark.skipif(
    not hasattr(pcre, "PcreErrorBadOptions") or not hasattr(BACKEND, "PCRE2_ERROR_BAD_OPTIONS"),
    reason="PCRE2 build lacks BAD_OPTIONS error",
)
def test_low_level_compile_reports_bad_options():
    with pytest.raises(pcre.PcreErrorBadOptions) as info:
        BACKEND.compile("a", flags=0xFFFFFFFF)

    exc = info.value
    assert exc.macro == "PCRE2_ERROR_BAD_OPTIONS"
    assert exc.code == getattr(BACKEND, "PCRE2_ERROR_BAD_OPTIONS")
    assert exc.error_code is pcre.PcreErrorCode.BAD_OPTIONS


def test_substitution_with_invalid_numeric_group_reference_raises():
    # Regression test: patterns that only expose numeric group names through
    # DUPNAMES should still reject ``\1`` substitutions when no real capture
    # exists instead of crashing.
    pattern = rb"\D\w?\(?#comment)(?=foo)(?#comment)(?P<1>abc)"
    flags = (
        pcre.Flag.NO_UCP
        | pcre.Flag.NO_JIT
        | pcre.Flag.ANCHORED
        | pcre.Flag.DUPNAMES
        | pcre.Flag.EXTENDED
        | pcre.Flag.UNGREEDY
    )
    compiled = pcre.compile(pattern, flags=flags)

    with pytest.raises(pcre.PcreError) as info:
        compiled.sub(b"\\1", b"Zfooabc")

    assert "invalid group reference" in str(info.value)
