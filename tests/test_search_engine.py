"""Host tests for the search module: every parser runs against a body captured
from its real endpoint, so none of this needs a console or a network."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "search-engine"


class SearchEngineTests(unittest.TestCase):
    def test_parsers_scoring_and_cache(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            self.skipTest("a C++ compiler is required")
        sources = [
            MODULE / "src/query.cpp",
            MODULE / "src/providers.cpp",
            MODULE / "src/scan.cpp",
            MODULE / "src/cache.cpp",
        ]
        with tempfile.TemporaryDirectory(prefix="search-engine-") as scratch:
            binary = Path(scratch) / "search-engine-test"
            subprocess.run(
                [
                    compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(MODULE / "include"), "-I" + str(MODULE / "src"),
                    str(MODULE / "tests/search_engine_test.cpp"),
                    *[str(path) for path in sources],
                    "-o", str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary), str(MODULE / "tests/fixtures")], check=True)


if __name__ == "__main__":
    unittest.main()
