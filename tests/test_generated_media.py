import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GeneratedMediaTests(unittest.TestCase):
    def test_only_generated_audio_is_playable(self):
        directory = ROOT / "build/tests"
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="generated-media-", dir=directory) as scratch:
            binary = Path(scratch) / "generated-media-test"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O1",
                            "-I" + str(ROOT / "include"), str(ROOT / "tests/generated_media_test.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
