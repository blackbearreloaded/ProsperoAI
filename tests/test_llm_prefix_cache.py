"""Opt-in real llama.cpp test; the ordinary suite needs no model or dependency build."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LLMPrefixCacheTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('LLAMA_HOST_BUILD') and os.environ.get('LLAMA_TEST_MODEL'),
                         'set LLAMA_HOST_BUILD and LLAMA_TEST_MODEL for real-model cache checks')
    def test_cached_and_cold_outputs(self):
        dependency = Path(os.environ.get('LLAMA_SOURCE', ROOT / '.deps/llama.cpp')).resolve()
        build = Path(os.environ['LLAMA_HOST_BUILD']).resolve()
        archives = [build / name for name in ['src/libllama.a', 'ggml/src/libggml.a',
                    'ggml/src/libggml-cpu.a', 'ggml/src/libggml-base.a']]
        flags = (build.parent/'compile_commands.json').read_text() if (build.parent/'compile_commands.json').exists() else ''
        tensor_name_bytes = '160' if 'GGML_MAX_NAME=160' in flags else '64'
        vk = build/'ggml/src/ggml-vulkan/libggml-vulkan.a'
        backend_libraries = [str(vk), '-lvulkan'] if vk.exists() else []
        with tempfile.TemporaryDirectory(prefix='llm-prefix-') as tmp:
            binary = Path(tmp) / 'test'
            subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O2',
                            '-ffp-contract=off', '-fsanitize=address,undefined', '-DGGML_MAX_NAME='+tensor_name_bytes, '-I'+str(ROOT/'include'),
                            '-I'+str(dependency/'include'), '-I'+str(dependency/'ggml/include'),
                            str(ROOT/'tests/llm_prefix_cache_test.cpp'), str(ROOT/'src/kv_budget.cpp'),
                            *map(str, archives), *backend_libraries, '-pthread', '-ldl', '-lm', '-o', str(binary)], check=True)
            subprocess.run([str(binary), os.environ['LLAMA_TEST_MODEL']], check=True, timeout=120)
