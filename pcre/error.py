# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

"""Static aliases for PCRE2 error exception types.

The PCRE2 C extension dynamically creates ``PcreError*`` subclasses when the
module is imported. This file mirrors that data so the Python layer exposes
stable names that IDEs and static analysis tools can discover without executing
C code. Values are generated from ``pcre_ext/pcre2.h``.
"""

from __future__ import annotations

from enum import IntEnum
from typing import Dict, List, Type


try:  # pragma: no cover - defensive fallback for documentation builds
    import pcre_ext_c as _backend
except ModuleNotFoundError:  # pragma: no cover - fallback when extension missing
    _backend = None

    class _BasePcreError(Exception):
        """Fallback base class used when the native extension is unavailable."""

        code: int
        macro: str

else:
    _BasePcreError = getattr(_backend, "PcreError")

__all__: List[str] = []
ERRORS_BY_CODE: Dict[int, List[Type[_BasePcreError]]] = {}
ERRORS_BY_MACRO: Dict[str, Type[_BasePcreError]] = {}



class PcreErrorCode(IntEnum):
    """IntEnum exposing PCRE2 error identifiers."""
    END_BACKSLASH: int = 101
    END_BACKSLASH_C: int = 102
    UNKNOWN_ESCAPE: int = 103
    QUANTIFIER_OUT_OF_ORDER: int = 104
    QUANTIFIER_TOO_BIG: int = 105
    MISSING_SQUARE_BRACKET: int = 106
    ESCAPE_INVALID_IN_CLASS: int = 107
    CLASS_RANGE_ORDER: int = 108
    QUANTIFIER_INVALID: int = 109
    INTERNAL_UNEXPECTED_REPEAT: int = 110
    INVALID_AFTER_PARENS_QUERY: int = 111
    POSIX_CLASS_NOT_IN_CLASS: int = 112
    POSIX_NO_SUPPORT_COLLATING: int = 113
    MISSING_CLOSING_PARENTHESIS: int = 114
    BAD_SUBPATTERN_REFERENCE: int = 115
    NULL_PATTERN: int = 116
    BAD_OPTIONS: int = 117
    MISSING_COMMENT_CLOSING: int = 118
    PARENTHESES_NEST_TOO_DEEP: int = 119
    PATTERN_TOO_LARGE: int = 120
    HEAP_FAILED: int = 121
    UNMATCHED_CLOSING_PARENTHESIS: int = 122
    INTERNAL_CODE_OVERFLOW: int = 123
    MISSING_CONDITION_CLOSING: int = 124
    LOOKBEHIND_NOT_FIXED_LENGTH: int = 125
    ZERO_RELATIVE_REFERENCE: int = 126
    TOO_MANY_CONDITION_BRANCHES: int = 127
    CONDITION_ASSERTION_EXPECTED: int = 128
    BAD_RELATIVE_REFERENCE: int = 129
    UNKNOWN_POSIX_CLASS: int = 130
    INTERNAL_STUDY_ERROR: int = 131
    UNICODE_NOT_SUPPORTED: int = 132
    PARENTHESES_STACK_CHECK: int = 133
    CODE_POINT_TOO_BIG: int = 134
    LOOKBEHIND_TOO_COMPLICATED: int = 135
    LOOKBEHIND_INVALID_BACKSLASH_C: int = 136
    UNSUPPORTED_ESCAPE_SEQUENCE: int = 137
    CALLOUT_NUMBER_TOO_BIG: int = 138
    MISSING_CALLOUT_CLOSING: int = 139
    ESCAPE_INVALID_IN_VERB: int = 140
    UNRECOGNIZED_AFTER_QUERY_P: int = 141
    MISSING_NAME_TERMINATOR: int = 142
    DUPLICATE_SUBPATTERN_NAME: int = 143
    INVALID_SUBPATTERN_NAME: int = 144
    UNICODE_PROPERTIES_UNAVAILABLE: int = 145
    MALFORMED_UNICODE_PROPERTY: int = 146
    UNKNOWN_UNICODE_PROPERTY: int = 147
    SUBPATTERN_NAME_TOO_LONG: int = 148
    TOO_MANY_NAMED_SUBPATTERNS: int = 149
    CLASS_INVALID_RANGE: int = 150
    OCTAL_BYTE_TOO_BIG: int = 151
    INTERNAL_OVERRAN_WORKSPACE: int = 152
    INTERNAL_MISSING_SUBPATTERN: int = 153
    DEFINE_TOO_MANY_BRANCHES: int = 154
    BACKSLASH_O_MISSING_BRACE: int = 155
    INTERNAL_UNKNOWN_NEWLINE: int = 156
    BACKSLASH_G_SYNTAX: int = 157
    PARENS_QUERY_R_MISSING_CLOSING: int = 158
    VERB_ARGUMENT_NOT_ALLOWED: int = 159
    VERB_UNKNOWN: int = 160
    SUBPATTERN_NUMBER_TOO_BIG: int = 161
    SUBPATTERN_NAME_EXPECTED: int = 162
    INTERNAL_PARSED_OVERFLOW: int = 163
    INVALID_OCTAL: int = 164
    SUBPATTERN_NAMES_MISMATCH: int = 165
    MARK_MISSING_ARGUMENT: int = 166
    INVALID_HEXADECIMAL: int = 167
    BACKSLASH_C_SYNTAX: int = 168
    BACKSLASH_K_SYNTAX: int = 169
    INTERNAL_BAD_CODE_LOOKBEHINDS: int = 170
    BACKSLASH_N_IN_CLASS: int = 171
    CALLOUT_STRING_TOO_LONG: int = 172
    UNICODE_DISALLOWED_CODE_POINT: int = 173
    UTF_IS_DISABLED: int = 174
    UCP_IS_DISABLED: int = 175
    VERB_NAME_TOO_LONG: int = 176
    BACKSLASH_U_CODE_POINT_TOO_BIG: int = 177
    MISSING_OCTAL_OR_HEX_DIGITS: int = 178
    VERSION_CONDITION_SYNTAX: int = 179
    INTERNAL_BAD_CODE_AUTO_POSSESS: int = 180
    CALLOUT_NO_STRING_DELIMITER: int = 181
    CALLOUT_BAD_STRING_DELIMITER: int = 182
    BACKSLASH_C_CALLER_DISABLED: int = 183
    QUERY_BARJX_NEST_TOO_DEEP: int = 184
    BACKSLASH_C_LIBRARY_DISABLED: int = 185
    PATTERN_TOO_COMPLICATED: int = 186
    LOOKBEHIND_TOO_LONG: int = 187
    PATTERN_STRING_TOO_LONG: int = 188
    INTERNAL_BAD_CODE: int = 189
    INTERNAL_BAD_CODE_IN_SKIP: int = 190
    NO_SURROGATES_IN_UTF16: int = 191
    BAD_LITERAL_OPTIONS: int = 192
    SUPPORTED_ONLY_IN_UNICODE: int = 193
    INVALID_HYPHEN_IN_OPTIONS: int = 194
    ALPHA_ASSERTION_UNKNOWN: int = 195
    SCRIPT_RUN_NOT_AVAILABLE: int = 196
    TOO_MANY_CAPTURES: int = 197
    MISSING_OCTAL_DIGIT: int = 198
    BACKSLASH_K_IN_LOOKAROUND: int = 199
    MAX_VAR_LOOKBEHIND_EXCEEDED: int = 200
    PATTERN_COMPILED_SIZE_TOO_BIG: int = 201
    OVERSIZE_PYTHON_OCTAL: int = 202
    CALLOUT_CALLER_DISABLED: int = 203
    EXTRA_CASING_REQUIRES_UNICODE: int = 204
    TURKISH_CASING_REQUIRES_UTF: int = 205
    EXTRA_CASING_INCOMPATIBLE: int = 206
    ECLASS_NEST_TOO_DEEP: int = 207
    ECLASS_INVALID_OPERATOR: int = 208
    ECLASS_UNEXPECTED_OPERATOR: int = 209
    ECLASS_EXPECTED_OPERAND: int = 210
    ECLASS_MIXED_OPERATORS: int = 211
    ECLASS_HINT_SQUARE_BRACKET: int = 212
    PERL_ECLASS_UNEXPECTED_EXPR: int = 213
    PERL_ECLASS_EMPTY_EXPR: int = 214
    PERL_ECLASS_MISSING_CLOSE: int = 215
    PERL_ECLASS_UNEXPECTED_CHAR: int = 216
    NOMATCH: int = -1
    PARTIAL: int = -2
    UTF8_ERR1: int = -3
    UTF8_ERR2: int = -4
    UTF8_ERR3: int = -5
    UTF8_ERR4: int = -6
    UTF8_ERR5: int = -7
    UTF8_ERR6: int = -8
    UTF8_ERR7: int = -9
    UTF8_ERR8: int = -10
    UTF8_ERR9: int = -11
    UTF8_ERR10: int = -12
    UTF8_ERR11: int = -13
    UTF8_ERR12: int = -14
    UTF8_ERR13: int = -15
    UTF8_ERR14: int = -16
    UTF8_ERR15: int = -17
    UTF8_ERR16: int = -18
    UTF8_ERR17: int = -19
    UTF8_ERR18: int = -20
    UTF8_ERR19: int = -21
    UTF8_ERR20: int = -22
    UTF8_ERR21: int = -23
    UTF16_ERR1: int = -24
    UTF16_ERR2: int = -25
    UTF16_ERR3: int = -26
    UTF32_ERR1: int = -27
    UTF32_ERR2: int = -28
    BADDATA: int = -29
    MIXEDTABLES: int = -30
    BADMAGIC: int = -31
    BADMODE: int = -32
    BADOFFSET: int = -33
    BADOPTION: int = -34
    BADREPLACEMENT: int = -35
    BADUTFOFFSET: int = -36
    CALLOUT: int = -37
    DFA_BADRESTART: int = -38
    DFA_RECURSE: int = -39
    DFA_UCOND: int = -40
    DFA_UFUNC: int = -41
    DFA_UITEM: int = -42
    DFA_WSSIZE: int = -43
    INTERNAL: int = -44
    JIT_BADOPTION: int = -45
    JIT_STACKLIMIT: int = -46
    MATCHLIMIT: int = -47
    NOMEMORY: int = -48
    NOSUBSTRING: int = -49
    NOUNIQUESUBSTRING: int = -50
    NULL: int = -51
    RECURSELOOP: int = -52
    DEPTHLIMIT: int = -53
    RECURSIONLIMIT: int = -53
    UNAVAILABLE: int = -54
    UNSET: int = -55
    BADOFFSETLIMIT: int = -56
    BADREPESCAPE: int = -57
    REPMISSINGBRACE: int = -58
    BADSUBSTITUTION: int = -59
    BADSUBSPATTERN: int = -60
    TOOMANYREPLACE: int = -61
    BADSERIALIZEDDATA: int = -62
    HEAPLIMIT: int = -63
    CONVERT_SYNTAX: int = -64
    INTERNAL_DUPMATCH: int = -65
    DFA_UINVALID_UTF: int = -66
    INVALIDOFFSET: int = -67
    JIT_UNSUPPORTED: int = -68
    REPLACECASE: int = -69
    TOOLARGEREPLACE: int = -70


def _register(macro: str, code: int, camel: str) -> None:
    class_name = f"PcreError{camel}"
    exc_type = getattr(_backend, class_name, None) if _backend is not None else None
    if exc_type is None:
        exc_type = type(f"PyError{camel}", (_BasePcreError,), {})
    exc_type.code = code
    exc_type.macro = macro
    if not getattr(exc_type, "__doc__", None):
        exc_type.__doc__ = f"PCRE2 error for {macro} (code {code})."
    alias = f"PyError{camel}"
    globals()[alias] = exc_type
    __all__.append(alias)
    ERRORS_BY_MACRO.setdefault(macro, exc_type)
    ERRORS_BY_CODE.setdefault(code, []).append(exc_type)


for _macro, _code, _camel in [
    ('PCRE2_ERROR_END_BACKSLASH', 101, 'EndBackslash'),
    ('PCRE2_ERROR_END_BACKSLASH_C', 102, 'EndBackslashC'),
    ('PCRE2_ERROR_UNKNOWN_ESCAPE', 103, 'UnknownEscape'),
    ('PCRE2_ERROR_QUANTIFIER_OUT_OF_ORDER', 104, 'QuantifierOutOfOrder'),
    ('PCRE2_ERROR_QUANTIFIER_TOO_BIG', 105, 'QuantifierTooBig'),
    ('PCRE2_ERROR_MISSING_SQUARE_BRACKET', 106, 'MissingSquareBracket'),
    ('PCRE2_ERROR_ESCAPE_INVALID_IN_CLASS', 107, 'EscapeInvalidInClass'),
    ('PCRE2_ERROR_CLASS_RANGE_ORDER', 108, 'ClassRangeOrder'),
    ('PCRE2_ERROR_QUANTIFIER_INVALID', 109, 'QuantifierInvalid'),
    ('PCRE2_ERROR_INTERNAL_UNEXPECTED_REPEAT', 110, 'InternalUnexpectedRepeat'),
    ('PCRE2_ERROR_INVALID_AFTER_PARENS_QUERY', 111, 'InvalidAfterParensQuery'),
    ('PCRE2_ERROR_POSIX_CLASS_NOT_IN_CLASS', 112, 'PosixClassNotInClass'),
    ('PCRE2_ERROR_POSIX_NO_SUPPORT_COLLATING', 113, 'PosixNoSupportCollating'),
    ('PCRE2_ERROR_MISSING_CLOSING_PARENTHESIS', 114, 'MissingClosingParenthesis'),
    ('PCRE2_ERROR_BAD_SUBPATTERN_REFERENCE', 115, 'BadSubpatternReference'),
    ('PCRE2_ERROR_NULL_PATTERN', 116, 'NullPattern'),
    ('PCRE2_ERROR_BAD_OPTIONS', 117, 'BadOptions'),
    ('PCRE2_ERROR_MISSING_COMMENT_CLOSING', 118, 'MissingCommentClosing'),
    ('PCRE2_ERROR_PARENTHESES_NEST_TOO_DEEP', 119, 'ParenthesesNestTooDeep'),
    ('PCRE2_ERROR_PATTERN_TOO_LARGE', 120, 'PatternTooLarge'),
    ('PCRE2_ERROR_HEAP_FAILED', 121, 'HeapFailed'),
    ('PCRE2_ERROR_UNMATCHED_CLOSING_PARENTHESIS', 122, 'UnmatchedClosingParenthesis'),
    ('PCRE2_ERROR_INTERNAL_CODE_OVERFLOW', 123, 'InternalCodeOverflow'),
    ('PCRE2_ERROR_MISSING_CONDITION_CLOSING', 124, 'MissingConditionClosing'),
    ('PCRE2_ERROR_LOOKBEHIND_NOT_FIXED_LENGTH', 125, 'LookbehindNotFixedLength'),
    ('PCRE2_ERROR_ZERO_RELATIVE_REFERENCE', 126, 'ZeroRelativeReference'),
    ('PCRE2_ERROR_TOO_MANY_CONDITION_BRANCHES', 127, 'TooManyConditionBranches'),
    ('PCRE2_ERROR_CONDITION_ASSERTION_EXPECTED', 128, 'ConditionAssertionExpected'),
    ('PCRE2_ERROR_BAD_RELATIVE_REFERENCE', 129, 'BadRelativeReference'),
    ('PCRE2_ERROR_UNKNOWN_POSIX_CLASS', 130, 'UnknownPosixClass'),
    ('PCRE2_ERROR_INTERNAL_STUDY_ERROR', 131, 'InternalStudyError'),
    ('PCRE2_ERROR_UNICODE_NOT_SUPPORTED', 132, 'UnicodeNotSupported'),
    ('PCRE2_ERROR_PARENTHESES_STACK_CHECK', 133, 'ParenthesesStackCheck'),
    ('PCRE2_ERROR_CODE_POINT_TOO_BIG', 134, 'CodePointTooBig'),
    ('PCRE2_ERROR_LOOKBEHIND_TOO_COMPLICATED', 135, 'LookbehindTooComplicated'),
    ('PCRE2_ERROR_LOOKBEHIND_INVALID_BACKSLASH_C', 136, 'LookbehindInvalidBackslashC'),
    ('PCRE2_ERROR_UNSUPPORTED_ESCAPE_SEQUENCE', 137, 'UnsupportedEscapeSequence'),
    ('PCRE2_ERROR_CALLOUT_NUMBER_TOO_BIG', 138, 'CalloutNumberTooBig'),
    ('PCRE2_ERROR_MISSING_CALLOUT_CLOSING', 139, 'MissingCalloutClosing'),
    ('PCRE2_ERROR_ESCAPE_INVALID_IN_VERB', 140, 'EscapeInvalidInVerb'),
    ('PCRE2_ERROR_UNRECOGNIZED_AFTER_QUERY_P', 141, 'UnrecognizedAfterQueryP'),
    ('PCRE2_ERROR_MISSING_NAME_TERMINATOR', 142, 'MissingNameTerminator'),
    ('PCRE2_ERROR_DUPLICATE_SUBPATTERN_NAME', 143, 'DuplicateSubpatternName'),
    ('PCRE2_ERROR_INVALID_SUBPATTERN_NAME', 144, 'InvalidSubpatternName'),
    ('PCRE2_ERROR_UNICODE_PROPERTIES_UNAVAILABLE', 145, 'UnicodePropertiesUnavailable'),
    ('PCRE2_ERROR_MALFORMED_UNICODE_PROPERTY', 146, 'MalformedUnicodeProperty'),
    ('PCRE2_ERROR_UNKNOWN_UNICODE_PROPERTY', 147, 'UnknownUnicodeProperty'),
    ('PCRE2_ERROR_SUBPATTERN_NAME_TOO_LONG', 148, 'SubpatternNameTooLong'),
    ('PCRE2_ERROR_TOO_MANY_NAMED_SUBPATTERNS', 149, 'TooManyNamedSubpatterns'),
    ('PCRE2_ERROR_CLASS_INVALID_RANGE', 150, 'ClassInvalidRange'),
    ('PCRE2_ERROR_OCTAL_BYTE_TOO_BIG', 151, 'OctalByteTooBig'),
    ('PCRE2_ERROR_INTERNAL_OVERRAN_WORKSPACE', 152, 'InternalOverranWorkspace'),
    ('PCRE2_ERROR_INTERNAL_MISSING_SUBPATTERN', 153, 'InternalMissingSubpattern'),
    ('PCRE2_ERROR_DEFINE_TOO_MANY_BRANCHES', 154, 'DefineTooManyBranches'),
    ('PCRE2_ERROR_BACKSLASH_O_MISSING_BRACE', 155, 'BackslashOMissingBrace'),
    ('PCRE2_ERROR_INTERNAL_UNKNOWN_NEWLINE', 156, 'InternalUnknownNewline'),
    ('PCRE2_ERROR_BACKSLASH_G_SYNTAX', 157, 'BackslashGSyntax'),
    ('PCRE2_ERROR_PARENS_QUERY_R_MISSING_CLOSING', 158, 'ParensQueryRMissingClosing'),
    ('PCRE2_ERROR_VERB_ARGUMENT_NOT_ALLOWED', 159, 'VerbArgumentNotAllowed'),
    ('PCRE2_ERROR_VERB_UNKNOWN', 160, 'VerbUnknown'),
    ('PCRE2_ERROR_SUBPATTERN_NUMBER_TOO_BIG', 161, 'SubpatternNumberTooBig'),
    ('PCRE2_ERROR_SUBPATTERN_NAME_EXPECTED', 162, 'SubpatternNameExpected'),
    ('PCRE2_ERROR_INTERNAL_PARSED_OVERFLOW', 163, 'InternalParsedOverflow'),
    ('PCRE2_ERROR_INVALID_OCTAL', 164, 'InvalidOctal'),
    ('PCRE2_ERROR_SUBPATTERN_NAMES_MISMATCH', 165, 'SubpatternNamesMismatch'),
    ('PCRE2_ERROR_MARK_MISSING_ARGUMENT', 166, 'MarkMissingArgument'),
    ('PCRE2_ERROR_INVALID_HEXADECIMAL', 167, 'InvalidHexadecimal'),
    ('PCRE2_ERROR_BACKSLASH_C_SYNTAX', 168, 'BackslashCSyntax'),
    ('PCRE2_ERROR_BACKSLASH_K_SYNTAX', 169, 'BackslashKSyntax'),
    ('PCRE2_ERROR_INTERNAL_BAD_CODE_LOOKBEHINDS', 170, 'InternalBadCodeLookbehinds'),
    ('PCRE2_ERROR_BACKSLASH_N_IN_CLASS', 171, 'BackslashNInClass'),
    ('PCRE2_ERROR_CALLOUT_STRING_TOO_LONG', 172, 'CalloutStringTooLong'),
    ('PCRE2_ERROR_UNICODE_DISALLOWED_CODE_POINT', 173, 'UnicodeDisallowedCodePoint'),
    ('PCRE2_ERROR_UTF_IS_DISABLED', 174, 'UtfIsDisabled'),
    ('PCRE2_ERROR_UCP_IS_DISABLED', 175, 'UcpIsDisabled'),
    ('PCRE2_ERROR_VERB_NAME_TOO_LONG', 176, 'VerbNameTooLong'),
    ('PCRE2_ERROR_BACKSLASH_U_CODE_POINT_TOO_BIG', 177, 'BackslashUCodePointTooBig'),
    ('PCRE2_ERROR_MISSING_OCTAL_OR_HEX_DIGITS', 178, 'MissingOctalOrHexDigits'),
    ('PCRE2_ERROR_VERSION_CONDITION_SYNTAX', 179, 'VersionConditionSyntax'),
    ('PCRE2_ERROR_INTERNAL_BAD_CODE_AUTO_POSSESS', 180, 'InternalBadCodeAutoPossess'),
    ('PCRE2_ERROR_CALLOUT_NO_STRING_DELIMITER', 181, 'CalloutNoStringDelimiter'),
    ('PCRE2_ERROR_CALLOUT_BAD_STRING_DELIMITER', 182, 'CalloutBadStringDelimiter'),
    ('PCRE2_ERROR_BACKSLASH_C_CALLER_DISABLED', 183, 'BackslashCCallerDisabled'),
    ('PCRE2_ERROR_QUERY_BARJX_NEST_TOO_DEEP', 184, 'QueryBarjxNestTooDeep'),
    ('PCRE2_ERROR_BACKSLASH_C_LIBRARY_DISABLED', 185, 'BackslashCLibraryDisabled'),
    ('PCRE2_ERROR_PATTERN_TOO_COMPLICATED', 186, 'PatternTooComplicated'),
    ('PCRE2_ERROR_LOOKBEHIND_TOO_LONG', 187, 'LookbehindTooLong'),
    ('PCRE2_ERROR_PATTERN_STRING_TOO_LONG', 188, 'PatternStringTooLong'),
    ('PCRE2_ERROR_INTERNAL_BAD_CODE', 189, 'InternalBadCode'),
    ('PCRE2_ERROR_INTERNAL_BAD_CODE_IN_SKIP', 190, 'InternalBadCodeInSkip'),
    ('PCRE2_ERROR_NO_SURROGATES_IN_UTF16', 191, 'NoSurrogatesInUtf16'),
    ('PCRE2_ERROR_BAD_LITERAL_OPTIONS', 192, 'BadLiteralOptions'),
    ('PCRE2_ERROR_SUPPORTED_ONLY_IN_UNICODE', 193, 'SupportedOnlyInUnicode'),
    ('PCRE2_ERROR_INVALID_HYPHEN_IN_OPTIONS', 194, 'InvalidHyphenInOptions'),
    ('PCRE2_ERROR_ALPHA_ASSERTION_UNKNOWN', 195, 'AlphaAssertionUnknown'),
    ('PCRE2_ERROR_SCRIPT_RUN_NOT_AVAILABLE', 196, 'ScriptRunNotAvailable'),
    ('PCRE2_ERROR_TOO_MANY_CAPTURES', 197, 'TooManyCaptures'),
    ('PCRE2_ERROR_MISSING_OCTAL_DIGIT', 198, 'MissingOctalDigit'),
    ('PCRE2_ERROR_BACKSLASH_K_IN_LOOKAROUND', 199, 'BackslashKInLookaround'),
    ('PCRE2_ERROR_MAX_VAR_LOOKBEHIND_EXCEEDED', 200, 'MaxVarLookbehindExceeded'),
    ('PCRE2_ERROR_PATTERN_COMPILED_SIZE_TOO_BIG', 201, 'PatternCompiledSizeTooBig'),
    ('PCRE2_ERROR_OVERSIZE_PYTHON_OCTAL', 202, 'OversizePythonOctal'),
    ('PCRE2_ERROR_CALLOUT_CALLER_DISABLED', 203, 'CalloutCallerDisabled'),
    ('PCRE2_ERROR_EXTRA_CASING_REQUIRES_UNICODE', 204, 'ExtraCasingRequiresUnicode'),
    ('PCRE2_ERROR_TURKISH_CASING_REQUIRES_UTF', 205, 'TurkishCasingRequiresUtf'),
    ('PCRE2_ERROR_EXTRA_CASING_INCOMPATIBLE', 206, 'ExtraCasingIncompatible'),
    ('PCRE2_ERROR_ECLASS_NEST_TOO_DEEP', 207, 'EclassNestTooDeep'),
    ('PCRE2_ERROR_ECLASS_INVALID_OPERATOR', 208, 'EclassInvalidOperator'),
    ('PCRE2_ERROR_ECLASS_UNEXPECTED_OPERATOR', 209, 'EclassUnexpectedOperator'),
    ('PCRE2_ERROR_ECLASS_EXPECTED_OPERAND', 210, 'EclassExpectedOperand'),
    ('PCRE2_ERROR_ECLASS_MIXED_OPERATORS', 211, 'EclassMixedOperators'),
    ('PCRE2_ERROR_ECLASS_HINT_SQUARE_BRACKET', 212, 'EclassHintSquareBracket'),
    ('PCRE2_ERROR_PERL_ECLASS_UNEXPECTED_EXPR', 213, 'PerlEclassUnexpectedExpr'),
    ('PCRE2_ERROR_PERL_ECLASS_EMPTY_EXPR', 214, 'PerlEclassEmptyExpr'),
    ('PCRE2_ERROR_PERL_ECLASS_MISSING_CLOSE', 215, 'PerlEclassMissingClose'),
    ('PCRE2_ERROR_PERL_ECLASS_UNEXPECTED_CHAR', 216, 'PerlEclassUnexpectedChar'),
    ('PCRE2_ERROR_NOMATCH', -1, 'Nomatch'),
    ('PCRE2_ERROR_PARTIAL', -2, 'Partial'),
    ('PCRE2_ERROR_UTF8_ERR1', -3, 'Utf8Err1'),
    ('PCRE2_ERROR_UTF8_ERR2', -4, 'Utf8Err2'),
    ('PCRE2_ERROR_UTF8_ERR3', -5, 'Utf8Err3'),
    ('PCRE2_ERROR_UTF8_ERR4', -6, 'Utf8Err4'),
    ('PCRE2_ERROR_UTF8_ERR5', -7, 'Utf8Err5'),
    ('PCRE2_ERROR_UTF8_ERR6', -8, 'Utf8Err6'),
    ('PCRE2_ERROR_UTF8_ERR7', -9, 'Utf8Err7'),
    ('PCRE2_ERROR_UTF8_ERR8', -10, 'Utf8Err8'),
    ('PCRE2_ERROR_UTF8_ERR9', -11, 'Utf8Err9'),
    ('PCRE2_ERROR_UTF8_ERR10', -12, 'Utf8Err10'),
    ('PCRE2_ERROR_UTF8_ERR11', -13, 'Utf8Err11'),
    ('PCRE2_ERROR_UTF8_ERR12', -14, 'Utf8Err12'),
    ('PCRE2_ERROR_UTF8_ERR13', -15, 'Utf8Err13'),
    ('PCRE2_ERROR_UTF8_ERR14', -16, 'Utf8Err14'),
    ('PCRE2_ERROR_UTF8_ERR15', -17, 'Utf8Err15'),
    ('PCRE2_ERROR_UTF8_ERR16', -18, 'Utf8Err16'),
    ('PCRE2_ERROR_UTF8_ERR17', -19, 'Utf8Err17'),
    ('PCRE2_ERROR_UTF8_ERR18', -20, 'Utf8Err18'),
    ('PCRE2_ERROR_UTF8_ERR19', -21, 'Utf8Err19'),
    ('PCRE2_ERROR_UTF8_ERR20', -22, 'Utf8Err20'),
    ('PCRE2_ERROR_UTF8_ERR21', -23, 'Utf8Err21'),
    ('PCRE2_ERROR_UTF16_ERR1', -24, 'Utf16Err1'),
    ('PCRE2_ERROR_UTF16_ERR2', -25, 'Utf16Err2'),
    ('PCRE2_ERROR_UTF16_ERR3', -26, 'Utf16Err3'),
    ('PCRE2_ERROR_UTF32_ERR1', -27, 'Utf32Err1'),
    ('PCRE2_ERROR_UTF32_ERR2', -28, 'Utf32Err2'),
    ('PCRE2_ERROR_BADDATA', -29, 'Baddata'),
    ('PCRE2_ERROR_MIXEDTABLES', -30, 'Mixedtables'),
    ('PCRE2_ERROR_BADMAGIC', -31, 'Badmagic'),
    ('PCRE2_ERROR_BADMODE', -32, 'Badmode'),
    ('PCRE2_ERROR_BADOFFSET', -33, 'Badoffset'),
    ('PCRE2_ERROR_BADOPTION', -34, 'Badoption'),
    ('PCRE2_ERROR_BADREPLACEMENT', -35, 'Badreplacement'),
    ('PCRE2_ERROR_BADUTFOFFSET', -36, 'Badutfoffset'),
    ('PCRE2_ERROR_CALLOUT', -37, 'Callout'),
    ('PCRE2_ERROR_DFA_BADRESTART', -38, 'DfaBadrestart'),
    ('PCRE2_ERROR_DFA_RECURSE', -39, 'DfaRecurse'),
    ('PCRE2_ERROR_DFA_UCOND', -40, 'DfaUcond'),
    ('PCRE2_ERROR_DFA_UFUNC', -41, 'DfaUfunc'),
    ('PCRE2_ERROR_DFA_UITEM', -42, 'DfaUitem'),
    ('PCRE2_ERROR_DFA_WSSIZE', -43, 'DfaWssize'),
    ('PCRE2_ERROR_INTERNAL', -44, 'Internal'),
    ('PCRE2_ERROR_JIT_BADOPTION', -45, 'JitBadoption'),
    ('PCRE2_ERROR_JIT_STACKLIMIT', -46, 'JitStacklimit'),
    ('PCRE2_ERROR_MATCHLIMIT', -47, 'Matchlimit'),
    ('PCRE2_ERROR_NOMEMORY', -48, 'Nomemory'),
    ('PCRE2_ERROR_NOSUBSTRING', -49, 'Nosubstring'),
    ('PCRE2_ERROR_NOUNIQUESUBSTRING', -50, 'Nouniquesubstring'),
    ('PCRE2_ERROR_NULL', -51, 'Null'),
    ('PCRE2_ERROR_RECURSELOOP', -52, 'Recurseloop'),
    ('PCRE2_ERROR_DEPTHLIMIT', -53, 'Depthlimit'),
    ('PCRE2_ERROR_RECURSIONLIMIT', -53, 'Recursionlimit'),
    ('PCRE2_ERROR_UNAVAILABLE', -54, 'Unavailable'),
    ('PCRE2_ERROR_UNSET', -55, 'Unset'),
    ('PCRE2_ERROR_BADOFFSETLIMIT', -56, 'Badoffsetlimit'),
    ('PCRE2_ERROR_BADREPESCAPE', -57, 'Badrepescape'),
    ('PCRE2_ERROR_REPMISSINGBRACE', -58, 'Repmissingbrace'),
    ('PCRE2_ERROR_BADSUBSTITUTION', -59, 'Badsubstitution'),
    ('PCRE2_ERROR_BADSUBSPATTERN', -60, 'Badsubspattern'),
    ('PCRE2_ERROR_TOOMANYREPLACE', -61, 'Toomanyreplace'),
    ('PCRE2_ERROR_BADSERIALIZEDDATA', -62, 'Badserializeddata'),
    ('PCRE2_ERROR_HEAPLIMIT', -63, 'Heaplimit'),
    ('PCRE2_ERROR_CONVERT_SYNTAX', -64, 'ConvertSyntax'),
    ('PCRE2_ERROR_INTERNAL_DUPMATCH', -65, 'InternalDupmatch'),
    ('PCRE2_ERROR_DFA_UINVALID_UTF', -66, 'DfaUinvalidUtf'),
    ('PCRE2_ERROR_INVALIDOFFSET', -67, 'Invalidoffset'),
    ('PCRE2_ERROR_JIT_UNSUPPORTED', -68, 'JitUnsupported'),
    ('PCRE2_ERROR_REPLACECASE', -69, 'Replacecase'),
    ('PCRE2_ERROR_TOOLARGEREPLACE', -70, 'Toolargereplace'),
]:
    _register(_macro, _code, _camel)

# Cleanup helper names from the module namespace.
del _register
del _macro
del _code
del _camel

__all__.extend(["ERRORS_BY_CODE", "ERRORS_BY_MACRO", "PcreErrorCode"])
