"""Host checks for the optional MiniCPM5 preset and text-chat template."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class MiniCPM5Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='prospero-inference-')
        cls.binary = Path(cls.directory.name) / 'check'
        subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O2',
                        '-Wall', '-Wextra', '-fsanitize=address,undefined',
                        '-I'+str(ROOT/'include'), str(ROOT/'tests/minicpm5_chat_test.cpp'),
                        '-o', str(cls.binary)], check=True)
    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()
    def test_recorded_text_chat_cases(self):
        cases = json.loads((ROOT/'tests/fixtures/minicpm5-chat-cases.json').read_text())
        for case in cases:
            args = [part for message in case['messages'] for part in [message['role'], message['content']]]
            actual = subprocess.check_output([str(self.binary), 'chat', *args], text=True)
            self.assertEqual(actual, case['expected'])
        self.assertEqual(subprocess.run([str(self.binary), 'chat', 'tool', 'result'], capture_output=True).returncode, 2)

    @unittest.skipUnless(importlib.util.find_spec('jinja2'), 'install Jinja2==3.1.6 for the upstream template oracle')
    def test_minicpm5_official_template(self):
        import jinja2
        template = jinja2.Environment().from_string((ROOT/'tests/fixtures/minicpm5-chat.jinja').read_text())
        cases = [
            [('user','Hello')],
            [('system','Answer briefly.'),('user','你好')],
            [('user','First'),('assistant','\nAnswer'),('user','Next')],
            [('user','First'),('assistant','<think>\nReason\n</think>\n\nAnswer'),('user','Next')],
            [('assistant','<think>\n\n</think>\n\nAnswer'),('user','Next')],
            [('assistant','prefix<think>\nA\n</think>\nB</think>\nAnswer'),('user','Next')],
            [('assistant','<think>unfinished'),('user','Next')],
            [('system',''),('user',''),('assistant',''),('system','New instruction'),('user','Next')],
        ]
        for pairs in cases:
            messages = [dict(role=role,content=content) for role,content in pairs]
            expected = template.render(messages=messages, bos_token='<s>', add_generation_prompt=True)
            actual = subprocess.check_output([str(self.binary),'chat',*[part for pair in pairs for part in pair]],text=True)
            self.assertEqual(actual,expected)
        self.assertEqual(subprocess.run([str(self.binary),'chat','tool','result'],capture_output=True).returncode,2)
    def test_preset_pin_and_generated_catalog(self):
        models = json.loads((ROOT/'model-tools/presets.json').read_text())
        entry = next(model for model in models if model['id']=='minicpm5-2b-q8-0.gguf')
        self.assertEqual(entry['repository'],'openbmb/MiniCPM5-2B-GGUF')
        self.assertEqual(entry['revision'],'2079a22f3beaa4e306449978533478fe0522f4b3')
        self.assertEqual(entry['files'][0]['size'],2679710688)
        header = (ROOT/'include/model_presets.hpp').read_text()
        for value in [entry['id'],entry['revision'],entry['files'][0]['sha256']]:
            self.assertIn(value,header)

    def test_existing_defaults_and_optional_catalog(self):
        models = json.loads((ROOT/'model-tools/presets.json').read_text())
        self.assertEqual([m['id'] for m in models[:3]], [
            'sd-turbo-fp16', 'stable-audio-open-small', 'kokoro-82m-fp16'])
        self.assertEqual(models[3]['id'], 'minicpm5-2b-q8-0.gguf')
        header = (ROOT/'include/model_presets.hpp').read_text()
        self.assertLess(header.index('"mistral-7b-instruct'), header.index('"minicpm5-2b'))
        self.assertLess(header.index('"qwen3.5-9b'), header.index('"minicpm5-2b'))
