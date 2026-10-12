import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class VulkanRouterTests(unittest.TestCase):
    def test_text_media_switching_and_catalog_refresh(self):
        for experimental in (False, True):
            with self.subTest(media_vulkan=experimental), tempfile.TemporaryDirectory(prefix='prospero-hybrid-') as directory:
                binary=Path(directory)/'hybrid'
                definitions=['-DPROSPERO_MEDIA_VULKAN'] if experimental else []
                subprocess.run([os.environ.get('HOST_CXX','clang++'),'-std=c++20','-O2','-pthread',
                    '-DPROSPERO_VULKAN_RUNTIME', *definitions, '-DPROSPERO_MODEL_ROOT="'+directory+'/models"',
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'inference'),
                    str(ROOT/'tests/vulkan_router_test.cpp'),
                    str(ROOT/'inference/router.cpp'),str(ROOT/'inference/adapters/registry.cpp'),
                    '-o',str(binary)],check=True)
                subprocess.run([str(binary)],check=True)
