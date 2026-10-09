import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class HybridRuntimeTests(unittest.TestCase):
    def test_text_media_switching_and_catalog_refresh(self):
        with tempfile.TemporaryDirectory(prefix='prospero-hybrid-') as directory:
            binary=Path(directory)/'hybrid'
            subprocess.run([os.environ.get('HOST_CXX','clang++'),'-std=c++20','-O2','-pthread',
                '-DPROSPERO_HYBRID_MEDIA', '-DPROSPERO_MODEL_ROOT="'+directory+'/models"',
                '-I'+str(ROOT/'include'),str(ROOT/'tests/hybrid_runtime_test.cpp'),
                str(ROOT/'vulkan/gpt_runtime_hybrid.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
