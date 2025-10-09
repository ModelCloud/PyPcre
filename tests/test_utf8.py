# SPDX-FileCopyrightText: 2025 ModelCloud.ai
# SPDX-FileCopyrightText: 2025 qubitium@modelcloud.ai
# SPDX-License-Identifier: Apache-2.0
# Contact: qubitium@modelcloud.ai, x.com/qubitium

import unittest

import pcre
import pcre_ext_c


BACKEND = pcre_ext_c
BACKEND_IS_FALLBACK = getattr(BACKEND, "__name__", "") == "pcre._fallback"


class TestUTF8Coverage(unittest.TestCase):
    def test_wide_language_literals_are_matchable(self):
        samples = [
            ("latin_basic", "hello"),
            ("latin_extended", "français"),
            ("german", "Straße"),
            ("turkish", "İstanbul"),
            ("icelandic", "Þjóð"),
            ("polish", "język"),
            ("greek", "Καλημέρα"),
            ("cyrillic", "Привет"),
            ("serbian", "Ћирилица"),
            ("ukrainian", "Україна"),
            ("hebrew", "שלום"),
            ("arabic", "مرحبا"),
            ("persian", "دوست"),
            ("syriac", "ܫܠܡܐ"),
            ("devanagari", "नमस्ते"),
            ("bengali", "বাংলা"),
            ("gurmukhi", "ਸਤਿ"),
            ("gujarati", "નમસ્તે"),
            ("tamil", "தமிழ்"),
            ("malayalam", "മലയാളം"),
            ("telugu", "నమస్కారం"),
            ("kannada", "ನಮಸ್ಕಾರ"),
            ("sinhala", "ආයුබෝවන්"),
            ("thai", "สวัสดี"),
            ("lao", "ສະບາຍດີ"),
            ("khmer", "សួស្តី"),
            ("myanmar", "မင်္ဂလာပါ"),
            ("tibetan", "བོད"),
            ("mongolian", "Сайн"),
            ("georgian", "ქართული"),
            ("armenian", "Բարեւ"),
            ("ethiopic", "ሰላም"),
            ("cherokee", "ᎣᏏᏲ"),
            ("canadian_syllabics", "ᓀᐦᐃᔭᐍᐏᐣ"),
            ("tifinagh", "ⵜⴰⵎⴰⵣⵉⵖⵜ"),
            ("osmanya", "𐒆𐒇𐒘"),
            ("nko", "ߒߞߏ"),
            ("vai", "ꕙꔤ"),
            ("yi", "ꆈꌠꁱ"),
            ("han", "漢字"),
            ("hiragana", "こんにちは"),
            ("katakana", "カタカナ"),
            ("hangul", "안녕하세요"),
        ]

        for language, sample in samples:
            with self.subTest(language=language):
                pattern = pcre.compile(sample)
                match = pattern.fullmatch(sample)
                self.assertIsNotNone(match, sample)
                self.assertEqual(match.group(0), sample)

    def test_multilingual_tokenisation_matches_expectations(self):
        tokens = [
            "Hello",
            "Привет",
            "مرحبا",
            "שלום",
            "漢字",
            "こんにちは",
            "안녕하세요",
            "नमस्ते",
            "தமிழ்",
            "বাংলা",
            "ⵜⴰⵎⴰⵣⵉⵖⵜ",
        ]
        text = " ".join(tokens)

        matches = pcre.findall(r"\S+", text)
        self.assertEqual(matches, tokens)

    def test_case_insensitive_comparisons_across_scripts(self):
        if BACKEND_IS_FALLBACK:
            self.skipTest("Fallback backend case folding mirrors Python's re module")

        pairs = [
            ("Straße", "straße"),
            ("Καλημερα", "καλημερα"),
            ("Привет", "привет"),
        ]
        for pattern_text, candidate in pairs:
            with self.subTest(pattern=pattern_text):
                pattern = pcre.compile(pattern_text, pcre.Flag.CASELESS)
                self.assertIsNotNone(pattern.fullmatch(candidate))

    def test_emoji_sequences_are_handled(self):
        emojis = ["🙂", "🙃", "😉", "👩🏽‍💻", "👨‍👩‍👧‍👦", "🧑‍🚀", "🏳️‍🌈"]
        text = "Let's mix some emoji: 🙂🙃😉 and 👩🏽‍💻 with a family 👨‍👩‍👧‍👦 plus 🧑‍🚀 and 🏳️‍🌈."

        for emoji in emojis:
            with self.subTest(emoji=emoji):
                match = pcre.search(emoji, text)
                self.assertIsNotNone(match)
                self.assertEqual(match.group(0), emoji)


if __name__ == "__main__":
    unittest.main()
