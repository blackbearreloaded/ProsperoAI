"""Exercise the Vulkan disk reader with real offset, multi-window and short reads."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModelIO(unittest.TestCase):
    def test_parallel_reads(self):
        with tempfile.TemporaryDirectory(prefix="prospero-model-io-") as directory:
            binary = Path(directory) / "reader"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++17",
                            "-pthread", "-I" + str(ROOT / "tools"),
                            str(ROOT / "tests/ps5_model_io_test.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=30)
