import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DownloaderFitTests(unittest.TestCase):
    def test_fit_thresholds(self):
        with tempfile.TemporaryDirectory(prefix="prospero-fit-") as directory:
            binary = Path(directory) / "fit"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(ROOT / "include"), "-I" + str(ROOT / ".deps/llama.cpp/vendor"),
                str(ROOT / "tests/downloader_fit_test.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
