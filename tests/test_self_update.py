"""The self-update engine against its helper: check, download, staging, apply and refusals."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "third_party/self-update-helper"


class SelfUpdateTests(unittest.TestCase):
    def test_engine_against_helper(self):
        cxx = os.environ.get("HOST_CXX", "clang++")
        cc = os.environ.get("HOST_CC", cxx.replace("clang++", "clang"))
        sanitize = ["-g", "-fsanitize=address,undefined"]
        with tempfile.TemporaryDirectory(prefix="prospero-self-update-") as directory:
            objects = []
            for name in ("miniz", "miniz_tinfl", "miniz_tdef", "miniz_zip"):
                objects.append(str(Path(directory) / f"{name}.o"))
                subprocess.run([cc, "-std=c11", "-O2", "-w", *sanitize, "-c",
                                str(ROOT / "third_party/miniz" / f"{name}.c"), "-o", objects[-1]],
                               check=True)
            binary = Path(directory) / "self-update"
            subprocess.run([
                cxx, "-std=c++20", "-O1", *sanitize, "-fno-sanitize-recover=all",
                "-I" + str(ROOT / "third_party"), "-I" + str(ROOT / "third_party/update-check"),
                "-I" + str(HELPER),
                str(HELPER / "test_self_update.cpp"), str(HELPER / "updater.cpp"),
                str(HELPER / "archive.cpp"), str(HELPER / "files.cpp"), *objects,
                "-pthread", "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
