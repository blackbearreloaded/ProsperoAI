"""Runs the browser-interface tests. They live as node test files beside this
one and were previously only runnable by hand."""

import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WebUiTests(unittest.TestCase):
    def test_node_test_files(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("node is required for the browser interface tests")
        files = sorted(str(path) for path in (ROOT / "tests").glob("*.mjs"))
        self.assertTrue(files, "no browser interface tests were found")
        subprocess.run([node, "--test", *files], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
