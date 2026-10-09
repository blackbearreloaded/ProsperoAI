"""Verify automatic console-local payload upload and storage readiness."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModelMountBootTests(unittest.TestCase):
    def test_local_payload_partial_transfer_and_storage_ready(self):
        with tempfile.TemporaryDirectory(prefix="prospero-auto-mount-") as directory:
            binary = Path(directory) / "boot"
            subprocess.run([
                os.environ.get("HOST_CXX", "clang++"), "-std=c++20", "-O2", "-pthread",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                '-DPROSPERO_MODEL_ROOT="' + directory + '/models"',
                "-I" + str(ROOT / "include"),
                "-I" + str(ROOT / ".deps/llama.cpp/vendor"),
                str(ROOT / "tests/model_mount_boot_test.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary), str(Path(directory) / "helper.elf")], check=True)
