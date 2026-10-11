"""Categorized work routes and storage adoption, without a console."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class StoragePathsTests(unittest.TestCase):
    def test_routes_and_non_overwriting_adoption(self):
        with tempfile.TemporaryDirectory(prefix="prospero-work-paths-") as directory:
            binary = Path(directory) / "paths"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O1",
                            "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
                            "-Wl,--gc-sections", "-I" + str(ROOT / "include"),
                            str(ROOT / "tests/storage_paths_test.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), directory], check=True)
