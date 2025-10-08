# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import re
import unittest

import pcre


try:
    import pcre2 as external_pcre2
except ImportError:  # pragma: no cover - optional dependency
    external_pcre2 = None


class TestRegexAccuracy(unittest.TestCase):
    """Cross-check pcre results against the stdlib re module."""

    SIMPLE_CASES = [
        (r"\bfoo\b", "foo bar foo"),
        (r"(?P<first>[A-Z][a-z]+)\s(?P<last>[A-Z][a-z]+)", "Alan Turing wrote code"),
        (r"(?:[A-Za-z]+-)*[A-Za-z]+", "well-known phrase"),
        (r"(?<!\S)(\w{3,})(?!\S)", "abc defg hij"),
    ]

    COMPLEX_CASES = [
        (r"(?:(?<=foo)bar|baz)(?!qux)", "foobar foobazqux baz"),
        (r"(?P<word>\b[A-Za-z]+\b)(?=.*\b\1\b)", "repeat word repeat"),
        (r"(?i:spam):-?\d+", "Spam:-10 spam:13"),
    ]

    def test_simple_patterns_match_re(self):
        for pattern_text, subject in self.SIMPLE_CASES:
            with self.subTest(pattern=pattern_text, subject=subject):
                re_pattern = re.compile(pattern_text)
                pcre_pattern = pcre.compile(pattern_text)
                expected = [(m.span(), m.groups(), m.groupdict()) for m in re_pattern.finditer(subject)]
                actual = [(m.span(), m.groups(), m.groupdict()) for m in pcre_pattern.finditer(subject)]
                self.assertEqual(actual, expected)

    def test_complex_patterns_match_re(self):
        for pattern_text, subject in self.COMPLEX_CASES:
            with self.subTest(pattern=pattern_text, subject=subject):
                re_pattern = re.compile(pattern_text)
                pcre_pattern = pcre.compile(pattern_text)
                expected = [m.span() for m in re_pattern.finditer(subject)]
                actual = [m.span() for m in pcre_pattern.finditer(subject)]
                self.assertEqual(actual, expected)

    def test_named_group_callback_replacement(self):
        pattern_text = r"(?P<word>[A-Za-z]+)"
        subject = "Hello world From Python"

        def re_replacement(match: re.Match[str]) -> str:
            word = match.group("word")
            return word.upper() if word.islower() else word.lower()

        def pcre_replacement(match: pcre.Match) -> str:
            word = match.group("word")
            return word.upper() if word.islower() else word.lower()

        expected = re.sub(pattern_text, re_replacement, subject)
        pcre_pattern = pcre.compile(pattern_text)
        actual_parts = []
        last = 0
        for match in pcre_pattern.finditer(subject):
            start, end = match.span()
            actual_parts.append(subject[last:start])
            actual_parts.append(pcre_replacement(match))
            last = end
        actual_parts.append(subject[last:])
        actual = "".join(actual_parts)
        self.assertEqual(actual, expected)

    def test_findall_alignment_with_re(self):
        pattern_text = r"(foo)(?=bar)"
        subject = "foobar fooqux foobar"
        re_results = re.findall(pattern_text, subject)
        pcre_results = pcre.findall(pattern_text, subject)
        self.assertEqual(pcre_results, re_results)

    @unittest.skipUnless(external_pcre2, "external pcre2 module is not installed")
    def test_external_pcre2_alignment(self):
        pattern_text = r"(?P<tag><([A-Za-z]+)>)(?P<body>.*?)(?P=tag)"
        subject = "<b>bold</b> <i>italic</i> plain"
        re_pattern = re.compile(pattern_text)
        pcre_pattern = pcre.compile(pattern_text)
        external_pattern = external_pcre2.compile(pattern_text)

        expected = [(m.groupdict(), m.span()) for m in re_pattern.finditer(subject)]
        actual = [(m.groupdict(), m.span()) for m in pcre_pattern.finditer(subject)]
        external = [(m.groupdict(), m.span()) for m in external_pattern.finditer(subject)]

        self.assertEqual(actual, expected)
        self.assertEqual(external, expected)


if __name__ == "__main__":
    unittest.main()
