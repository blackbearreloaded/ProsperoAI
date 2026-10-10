import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class PrefixReuseTests(unittest.TestCase):
    def test_common_prefix_rules(self):
        with tempfile.TemporaryDirectory(prefix="prospero-prefix-") as directory:
            binary = Path(directory) / "prefix-reuse"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/prefix_reuse_test.cpp"), str(ROOT / "src/prefix_reuse.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
