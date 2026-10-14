import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class VoiceRecorderTests(unittest.TestCase):
    def test_capture_limits_cancel_silence_and_device_failure(self):
        directory = ROOT / "build/tests"
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="voice-recorder-", dir=directory) as scratch:
            binary = Path(scratch) / "voice-recorder-test"
            subprocess.run([os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O1", "-pthread",
                            "-I" + str(ROOT / "include"), str(ROOT / "tests/voice_recorder_test.cpp"),
                            str(ROOT / "src/voice_recorder.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)
