import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DownloaderStorageTests(unittest.TestCase):
    def test_mounted_storage_and_streaming_verification(self):
        with tempfile.TemporaryDirectory(prefix="prospero-storage-") as directory:
            binary = Path(directory) / "storage"
            subprocess.run([
                os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O2",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(ROOT / "include"), "-I" + str(ROOT / ".deps/llama.cpp/vendor"), str(ROOT / "tests/downloader_storage_test.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
