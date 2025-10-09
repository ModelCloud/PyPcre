import unittest
import regex
import json
import pcre

class TestTransformersRegex(unittest.TestCase):
    def test_transformers_regex(self):
        json_list = []

        with open("transformers_regex_usages.jsonl", "r", encoding="utf-8") as f:
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
            print(f"index: {i} pattern_text: {pattern_text} subject: {subject}")
            with self.subTest(pattern=pattern_text, subject=subject):
                re_pattern = regex.compile(pattern_text)
                pcre_pattern = pcre.compile(pattern_text)
                expected = [(m.span(), m.groups(), m.groupdict()) for m in re_pattern.finditer(subject)]
                actual = [(m.span(), m.groups(), m.groupdict()) for m in pcre_pattern.finditer(subject)]
                self.assertEqual(actual, expected)
