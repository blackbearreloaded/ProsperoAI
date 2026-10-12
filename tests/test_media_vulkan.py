import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class MediaVulkanTests(unittest.TestCase):
    def test_real_vulkan_media_bridge_numerical_parity(self):
        libs = ROOT / 'build/llama-vulkan/host/bin'
        if not (libs / 'libggml-vulkan.so').exists():
            self.skipTest('make llama-vulkan required for real host Vulkan parity')
        with tempfile.TemporaryDirectory(prefix='prospero-media-vk-') as directory:
            binary=Path(directory)/'media'
            subprocess.run([os.environ.get('HOST_CXX','clang++'),'-std=c++20','-O1','-pthread',
                '-I'+str(ROOT/'include'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'.deps/llama.cpp/ggml/include'),
                str(ROOT/'tests/media_vulkan_test.cpp'),
                str(ROOT/'inference/backends/vulkan/media_bridge.cpp'),'-L'+str(libs),'-Wl,-rpath,'+str(libs),
                '-lggml-vulkan','-lggml','-lggml-base','-o',str(binary)],check=True)
            environment={**os.environ}
            software=Path('/usr/share/vulkan/icd.d/lvp_icd.x86_64.json')
            if software.exists():
                environment['VK_ICD_FILENAMES']=str(software)
                environment['GGML_VK_VISIBLE_DEVICES']='0'
            environment['GGML_NO_BACKTRACE']='1'
            subprocess.run([str(binary)],check=True,env=environment,timeout=120)
