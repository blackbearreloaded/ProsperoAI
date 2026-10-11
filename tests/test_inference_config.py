"""Host checks for bounded inference tuning input."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class InferenceConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='prospero-inference-')
        cls.binary = Path(cls.directory.name) / 'check'
        subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O2',
                        '-Wall', '-Wextra', '-fsanitize=address,undefined',
                        '-I'+str(ROOT/'include'), str(ROOT/'tests/inference_config_test.cpp'),
                        '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()
    def test_numeric_boundaries(self):
        subprocess.run([str(self.binary), self.directory.name], check=True)
