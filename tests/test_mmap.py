# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import gc
import mmap
import os
import tempfile
import unittest

import pcre


class TestMmapSubject(unittest.TestCase):
    def setUp(self):
        fd, self.path = tempfile.mkstemp()
        os.close(fd)
        self.data = b"prefix\x00foo\xffbar123baz needle-42 tail\n" * 50
        with open(self.path, "wb") as f:
            f.write(self.data)
        self._fp = open(self.path, "rb")
        self.mm = mmap.mmap(self._fp.fileno(), 0, access=mmap.ACCESS_READ)

    def tearDown(self):
        gc.collect()
        self.mm.close()
        self._fp.close()
        os.unlink(self.path)

    def test_search_returns_bytes_groups(self):
        pattern = pcre.compile(rb"needle-(\d+)")
        match = pattern.search(self.mm)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"needle-42")
        self.assertEqual(match.group(1), b"42")
        self.assertIsInstance(match.group(0), bytes)

    def test_match_at_start(self):
        pattern = pcre.compile(rb"prefix")
        match = pattern.match(self.mm)
        self.assertIsNotNone(match)
        self.assertEqual(match.span(), (0, 6))

    def test_fullmatch_on_exact_span(self):
        pattern = pcre.compile(rb"prefix")
        small = mmap.mmap(-1, len(b"prefix"))
        small.write(b"prefix")
        try:
            match = pattern.fullmatch(small)
            self.assertIsNotNone(match)
            self.assertEqual(match.span(), (0, 6))
            del match
        finally:
            small.close()

    def test_finditer_matches_same_as_bytes(self):
        pattern = pcre.compile(rb"needle-(\d+)")
        from_mmap = [m.span() for m in pattern.finditer(self.mm)]
        from_bytes = [m.span() for m in pattern.finditer(self.data)]
        self.assertEqual(from_mmap, from_bytes)
        self.assertEqual(len(from_mmap), 50)

    def test_findall(self):
        pattern = pcre.compile(rb"needle-(\d+)")
        self.assertEqual(pattern.findall(self.mm), [b"42"] * 50)

    def test_module_level_search(self):
        match = pcre.search(rb"foo\xffbar", self.mm)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"foo\xffbar")

    def test_pos_and_endpos_are_byte_offsets(self):
        pattern = pcre.compile(rb"prefix")
        first_end = pattern.match(self.mm).end()
        self.assertIsNone(pattern.search(self.mm, pos=1, endpos=first_end - 1))
        self.assertIsNotNone(pattern.search(self.mm, pos=0, endpos=first_end))

    def test_split_on_mmap(self):
        pattern = pcre.compile(rb"\s+")
        parts = pattern.split(self.mm)
        self.assertTrue(all(isinstance(p, bytes) for p in parts))

    def test_sub_on_mmap(self):
        pattern = pcre.compile(rb"needle-\d+")
        result = pattern.sub(b"X", self.mm)
        self.assertNotIn(b"needle-42", result)
        self.assertEqual(result.count(b"X"), 50)

    def test_noncontiguous_buffer_rejected(self):
        # pcre.Pattern materializes plain `memoryview` subjects via
        # .tobytes() before reaching the C extension (a pre-existing,
        # unrelated compatibility shim), so exercise the low-level binding
        # directly to confirm it refuses a non-contiguous buffer instead of
        # silently reading the wrong bytes.
        import array

        import pcre_ext_c as raw

        strided = memoryview(array.array("b", range(20)))[::2]
        self.assertFalse(strided.contiguous)
        pattern = raw.compile(rb"x")
        with self.assertRaises((TypeError, BufferError)):
            pattern.search(strided)

    def test_non_buffer_subject_still_rejected(self):
        pattern = pcre.compile(rb"x")
        with self.assertRaises(TypeError):
            pattern.search(12345)

    def test_mmap_match_uses_an_immutable_snapshot(self):
        fd, path = tempfile.mkstemp()
        os.close(fd)
        try:
            with open(path, "wb") as f:
                f.write(b"hello world")
            fp = open(path, "rb")
            mm = mmap.mmap(fp.fileno(), 0, access=mmap.ACCESS_READ)
            pattern = pcre.compile(rb"hello")
            match = pattern.search(mm)
            self.assertIsNotNone(match)
            mm.close()
            self.assertEqual(match.group(0), b"hello")
            del match
            gc.collect()
            fp.close()
        finally:
            os.unlink(path)

    def test_bytearray_subject_zero_copy_path(self):
        buf = bytearray(b"hello bytearray world")
        pattern = pcre.compile(rb"bytearray")
        match = pattern.search(buf)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(0), b"bytearray")


if __name__ == "__main__":
    unittest.main()
