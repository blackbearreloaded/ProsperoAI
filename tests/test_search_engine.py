"""Host tests for the search module: every parser runs against a body captured
from its real endpoint, so none of this needs a console or a network."""

import re
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

    def test_search_never_reaches_the_gpu(self):
        """Search must cost no video memory, which it earns by containing no
        graphics call at all. Sources first, then the console objects when a build
        is around: an import is what would make a GPU allocation possible."""
        # Graphics entry points and headers, not the PS5_LLAMA_VULKAN build macro,
        # which only names the console build this module is compiled into.
        forbidden = re.compile(r"\bvk[A-Z]\w+|\bVk[A-Z]\w+|\bggml_\w+|sceGnm\w*|sceVideoOut\w*"
                               r"|sceGpu\w*")
        included = re.compile(r"#\s*include.*(vulkan|ggml|gnm|videoout)", re.IGNORECASE)
        for source in sorted((MODULE / "src").glob("*.*")) + [MODULE / "include/prospero_search.hpp"]:
            for number, line in enumerate(source.read_text().splitlines(), 1):
                if line.lstrip().startswith("//") or line.lstrip().startswith("*"):
                    continue
                self.assertIsNone(forbidden.search(line),
                                  f"{source.name}:{number} calls the GPU: {line.strip()}")
                self.assertIsNone(included.search(line),
                                  f"{source.name}:{number} includes a graphics header: {line.strip()}")
        objects = sorted(Path(ROOT / "build/vulkan-runtime/obj").glob("search-engine_src_*.o"))
        if not objects:
            self.skipTest("no console objects to inspect; run tools/build-vulkan-native.sh first")
        nm = shutil.which("nm")
        if not nm:
            self.skipTest("nm is required to inspect the console objects")
        symbols = set()
        for object_file in objects:
            listing = subprocess.run([nm, "-u", str(object_file)], check=True, text=True,
                                     capture_output=True).stdout
            symbols.update(line.split()[-1] for line in listing.splitlines() if line.strip())
        offenders = sorted(symbol for symbol in symbols
                           if forbidden.search(symbol) or "vulkan" in symbol.lower())
        self.assertEqual(offenders, [], "the search module imports graphics symbols")


if __name__ == "__main__":
    unittest.main()
