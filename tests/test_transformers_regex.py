import json
import os
import unittest

import pcre
import regex


class TestTransformersRegex(unittest.TestCase):

    def setUp(self):
        pcre.configure(compat_regex=True)

    def tearDown(self):
        pcre.configure(compat_regex=False)

    def test_transformers_regex(self):
        json_list = []

        BASE_DIR = os.path.dirname(os.path.abspath(__file__))
        jsonl_path = os.path.join(BASE_DIR, "transformers_regex_usages.jsonl")

        with open(jsonl_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                    json_list.append(obj)
                except json.JSONDecodeError:
                    print("cannot parse line:", line[:80])

        print(f"total json: {len(json_list)}")

        for i, obj in enumerate(json_list):
            pattern_text = obj["pattern"]
            subject = obj["test_string"]
            pattern_repr = ascii(pattern_text)
            subject_repr = ascii(subject)
            print(f"index: {i} pattern_text: {pattern_repr} subject: {subject_repr}")
            try:
                with self.subTest(pattern=pattern_text, subject=subject):
                    re_pattern = regex.compile(pattern_text)
                    pcre_pattern = pcre.compile(pattern_text)

                    expected = [(m.span(), m.groups(), m.groupdict()) for m in re_pattern.finditer(subject)]
                    actual = [(m.span(), m.groups(), m.groupdict()) for m in pcre_pattern.finditer(subject)]
                    self.assertEqual(expected, actual)
            except regex.error as e:
                self.fail(f"Compile error for pattern:\n  {pattern_text}\n  Error: {type(e).__name__}: {e}")
            except pcre.error as e:
                self.fail(f"Compile error for pattern:\n  {pattern_text}\n  Error: {type(e).__name__}: {e}")
