import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class KvBudgetTests(unittest.TestCase):
    def test_auto_choice_rules(self):
        with tempfile.TemporaryDirectory(prefix="prospero-kv-") as directory:
            binary = Path(directory) / "kv-budget"
            compiler = os.environ.get("HOST_CXX", "clang++")
            subprocess.run([
                compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "include"),
                str(ROOT / "tests/kv_budget_test.cpp"), str(ROOT / "src/kv_budget.cpp"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
