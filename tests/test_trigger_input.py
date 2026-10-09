import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TriggerInputTests(unittest.TestCase):
    def test_analog_and_digital_trigger_edges(self):
        with tempfile.TemporaryDirectory(prefix="prospero-triggers-") as directory:
            binary = Path(directory) / "triggers"
            subprocess.run([
                os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O2",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I" + str(ROOT / "include"), str(ROOT / "tests/trigger_input_test.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
