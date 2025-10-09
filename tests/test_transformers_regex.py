import os
import unittest
import regex
import json
import pcre

_u4 = regex.compile(r'((?<!\\)(?:\\\\)*)\\u([0-9A-Fa-f]{4})')
_u8 = regex.compile(r'((?<!\\)(?:\\\\)*)\\U([0-9A-Fa-f]{8})')

def to_pcre_hex(pattern: str):
    count = 0

    def rep8(m):
        nonlocal count
        count += 1
        return m.group(1) + r'\x{' + m.group(2) + '}'

    def rep4(m):
        nonlocal count
        count += 1
        return m.group(1) + r'\x{' + m.group(2) + '}'

    new_pat = _u8.sub(rep8, pattern)
    new_pat = _u4.sub(rep4, new_pat)
    return new_pat


class TestTransformersRegex(unittest.TestCase):
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
            print(f"index: {i} pattern_text: {pattern_text} subject: {subject}")
            try:
                with self.subTest(pattern=pattern_text, subject=subject):
                    re_pattern = regex.compile(pattern_text)
                    pcre_pattern = pcre.compile(to_pcre_hex(pattern_text))

                    expected = [(m.span(), m.groups(), m.groupdict()) for m in re_pattern.finditer(subject)]
                    actual = [(m.span(), m.groups(), m.groupdict()) for m in pcre_pattern.finditer(subject)]
                    self.assertEqual(expected, actual)
            except regex.error as e:
                self.fail(f"Compile error for pattern:\n  {pattern_text}\n  Error: {type(e).__name__}: {e}")
            except pcre.error as e:
                self.fail(f"Compile error for pattern:\n  {pattern_text}\n  Error: {type(e).__name__}: {e}")
