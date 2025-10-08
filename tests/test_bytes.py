# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import unittest

import pcre


class TestBytesHandling(unittest.TestCase):
    def test_bytes_pattern_matches_high_bit_and_nuls(self):
        pattern = pcre.compile(br"\x00(?P<chunk>[\xff\xfe]+)\x01")
        data = b"\x00\xff\xfe\xfe\x01"

        match = pattern.fullmatch(data)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), data)
        self.assertEqual(match.group("chunk"), b"\xff\xfe\xfe")
        self.assertFalse(pattern.flags & pcre.Flag.UTF)
        self.assertFalse(pattern.flags & pcre.Flag.UCP)

    def test_bytes_pattern_accepts_text_subject(self):
        pattern = pcre.compile(br"data")
        match = pattern.search("data")
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), "data")

    def test_text_pattern_accepts_bytes_subject(self):
        pattern = pcre.compile(r"data")
        match = pattern.search(b"data")
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"data")

    def test_module_level_helpers_work_with_bytes(self):
        haystack = b"prefix\x00foo\xffbar\x10baz"

        match = pcre.match(br"prefix", haystack)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"prefix")

        search = pcre.search(br"\xffbar", haystack)
        self.assertIsNotNone(search)
        self.assertEqual(search.group(0), b"\xffbar")

        self.assertEqual(pcre.findall(br"[a-z]+", haystack), [b"prefix", b"foo", b"bar", b"baz"])

    def test_clear_distinction_between_bytes_and_text_cache(self):
        text_pattern = pcre.compile(r"cache")
        bytes_pattern = pcre.compile(br"cache")

        self.assertIsNot(text_pattern, bytes_pattern)
        self.assertIs(pcre.compile(r"cache"), text_pattern)
        self.assertIs(pcre.compile(br"cache"), bytes_pattern)


if __name__ == "__main__":
    unittest.main()
