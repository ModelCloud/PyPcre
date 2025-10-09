# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import threading
import unittest

import pcre
import pcre_ext_c
from pcre import Flag


BACKEND = pcre_ext_c
BACKEND_IS_FALLBACK = getattr(BACKEND, "__name__", "") == "pcre._fallback"


class TestPCRE2Basics(unittest.TestCase):
    def test_basic_match(self):
        pattern = pcre.compile(r"h.llo", Flag.CASELESS)
        match = pattern.match("Hello world")
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), "Hello")
        self.assertEqual(match.span(), (0, 5))

    def test_named_groups(self):
        pattern = pcre.compile(r"(?P<word>\w+)-(\d+)")
        match = pattern.search("id=token-1234;")
        self.assertIsNotNone(match)
        self.assertEqual(match.group("word"), "token")
        self.assertEqual(match.groupdict()["word"], "token")
        self.assertEqual(match.groups(), ("token", "1234"))

    def test_finditer_zero_width(self):
        pattern = pcre.compile(r"^|$", Flag.MULTILINE)
        matches = list(pattern.finditer("one\ntwo"))
        self.assertEqual(len(matches), 4)  # start/end for two lines

    def test_default_unicode_behaviour_matches_re(self):
        pattern = pcre.compile(r"\w+")
        self.assertTrue(pattern.flags & Flag.UTF)
        self.assertTrue(pattern.flags & Flag.UCP)
        self.assertEqual(pattern.match("Straße").group(0), "Straße")
        self.assertEqual(pattern.fullmatch("café").group(0), "café")

        bytes_pattern = pcre.compile(br"\w+")
        self.assertFalse(bytes_pattern.flags & Flag.UTF)
        self.assertFalse(bytes_pattern.flags & Flag.UCP)
        self.assertIsNone(bytes_pattern.match(b"\xdf"))

    def test_no_flags_disable_default_unicode_behaviour(self):
        pattern = pcre.compile(r"\w+", Flag.NO_UCP)
        self.assertFalse(pattern.flags & Flag.UCP)
        self.assertEqual(0, pattern.flags & int(Flag.NO_UCP))

        utf_pattern = pcre.compile(r"\w+", Flag.NO_UTF)
        self.assertFalse(utf_pattern.flags & Flag.UTF)
        self.assertEqual(0, utf_pattern.flags & int(Flag.NO_UTF))

    def test_unicode_properties_align_with_reference(self):
        if BACKEND_IS_FALLBACK:
            self.skipTest("Unicode property escapes require the native PCRE2 backend")
        try:
            import pcre2 as reference
        except ModuleNotFoundError:  # pragma: no cover - optional dependency
            self.skipTest("external pcre2 package not available")

        subject = "Straße Καλημέρα 123"

        property_pattern = r"\p{L}+"
        self.assertEqual(
            [match.group(0) for match in pcre.compile(property_pattern).finditer(subject)],
            [match.group(0) for match in reference.compile(property_pattern).finditer(subject)],
        )

        script_pattern = r"\p{Greek}+"
        self.assertEqual(
            pcre.findall(script_pattern, subject),
            reference.findall(script_pattern, subject),
        )

    def test_bytes_subject(self):
        pattern = pcre.compile(b"a+(?P<num>\\d+)")
        match = pattern.search(b"xxxaa123")
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"aa123")
        self.assertEqual(match.group("num"), b"123")

    def test_fullmatch(self):
        pattern = pcre.compile(r"\d{4}-\d{2}-\d{2}")
        self.assertIsNotNone(pattern.fullmatch("2025-10-08"))
        self.assertIsNone(pattern.fullmatch("08/10/2025"))

    def test_module_shortcuts(self):
        match = pcre.match(r"foo", "foobar")
        self.assertIsNotNone(match)
        search = pcre.search(r"bar", "foobar")
        self.assertIsNotNone(search)
        self.assertEqual(pcre.findall(r"a+", "caaab"), ["aaa"])

    def test_compile_cache_reuses_pattern(self):
        first = pcre.compile(r"cache-me")
        second = pcre.compile(r"cache-me")
        self.assertIs(first, second)

        pcre.match(r"cache-me", "cache-me")
        self.assertIs(pcre.compile(r"cache-me"), first)

    def test_clear_cache(self):
        cached = pcre.compile(r"clear-me")
        pcre.clear_cache()
        refreshed = pcre.compile(r"clear-me")
        self.assertIsNot(cached, refreshed)

    def test_compile_pattern_with_flags_error(self):
        pattern = pcre.compile(r"flag-test")
        with self.assertRaises(ValueError):
            pcre.compile(pattern, Flag.CASELESS)

    def test_compile_cache_distinguishes_flags(self):
        base = pcre.compile(r"flagged")
        upper = pcre.compile(r"flagged", Flag.CASELESS)
        again_upper = pcre.compile(r"flagged", Flag.CASELESS)
        self.assertIsNot(base, upper)
        self.assertIs(upper, again_upper)

    def test_threaded_matches(self):
        pattern = pcre.compile(r"(foo)+")
        subject = "foo" * 1000
        errors = []

        def worker():
            try:
                match = pattern.search(subject)
                if match is None or match.group(0) != subject:
                    errors.append("unexpected result")
            except Exception as exc:  # pragma: no cover - debugging aid
                errors.append(exc)

        threads = [threading.Thread(target=worker) for _ in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        self.assertFalse(errors, errors)

    def test_flag_enum_aliases(self):
        self.assertTrue(hasattr(pcre, "Flag"))
        self.assertFalse(hasattr(pcre, "PCRE2_MULTILINE"))
        self.assertFalse(hasattr(pcre, "NO_UTF"))
        self.assertEqual(int(Flag.MULTILINE), getattr(BACKEND, "PCRE2_MULTILINE"))
        combo = Flag.CASELESS | Flag.MULTILINE
        self.assertEqual(
            int(combo),
            getattr(BACKEND, "PCRE2_CASELESS") | getattr(BACKEND, "PCRE2_MULTILINE"),
        )


if __name__ == "__main__":
    unittest.main()
