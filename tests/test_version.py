import concurrent.futures
import threading
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

    def test_runtime_version_query_is_thread_safe(self) -> None:
        barrier = threading.Barrier(16)

        def query(_: int) -> str:
            barrier.wait(timeout=5)
            return pcre_ext_c.get_library_version()

        with concurrent.futures.ThreadPoolExecutor(max_workers=16) as executor:
            versions = list(executor.map(query, range(64)))

        self.assertEqual(len(set(versions)), 1)
        self.assertTrue(versions[0])


if __name__ == "__main__":
    unittest.main()
