import unittest

import pcre_ext_c


class PcreVersionTest(unittest.TestCase):
    def test_constant_matches_runtime_query(self) -> None:
        version_constant = getattr(pcre_ext_c, "PCRE2_VERSION", None)
        print(f"version_constant {version_constant}")
        self.assertIsInstance(version_constant, str)
        self.assertTrue(version_constant)

        runtime_version = pcre_ext_c.get_library_version()
        print(f"runtime_version {runtime_version}")
        self.assertIsInstance(runtime_version, str)
        self.assertTrue(runtime_version)

        self.assertEqual(runtime_version, version_constant)


if __name__ == "__main__":
    unittest.main()
