import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ps5_config",ROOT / "tools/ps5_config.py")
CONFIG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONFIG)

class ConsoleToolTests(unittest.TestCase):
    def setUp(self):
        directory=ROOT / "build/tests"
        directory.mkdir(parents=True,exist_ok=True)
        self.scratch=tempfile.TemporaryDirectory(prefix="ps5ctl-",dir=directory)
        self.config=Path(self.scratch.name)/"local.env"

    def tearDown(self):
        self.scratch.cleanup()

    def environment(self):
        env=dict(os.environ)
        for key in CONFIG.KEYS:
            env.pop(key,None)
        env["PS5_ENV_FILE"]=str(self.config)
        return env

    def test_local_env_and_explicit_override_without_shell_execution(self):
        self.config.write_text('PS5_HOST=console.example\nP5_MANAGER=\nPROSPERO_TEST_TITLE=PPSA99999\n')
        with patch.dict(os.environ,self.environment(),clear=True):
            os.environ['PS5_HOST']='override.example'
            CONFIG.load_console_env()
            self.assertEqual(os.environ['PS5_HOST'],'override.example')
            self.assertEqual(os.environ['P5_MANAGER'],'')
            self.assertEqual(os.environ['PROSPERO_TEST_TITLE'],'PPSA99999')
        self.config.write_text('HOME=/unsafe\n')
        with patch.dict(os.environ,self.environment(),clear=True):
            with self.assertRaises(ValueError):CONFIG.load_console_env()

    def test_doctor_checks_bundles_without_requiring_optional_manager(self):
        self.config.write_text('PS5_HOST=console.example\nPROSPERO_TEST_TITLE=PPSA99999\n')
        run=subprocess.run(['python3',str(ROOT/'tools/ps5ctl.py'),'doctor'],env=self.environment(),text=True,capture_output=True)
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertIn('P5_MANAGER: unset (optional',run.stdout)
        self.assertEqual(run.stdout.count(': verified tools/payloads/'),4)

    def test_missing_host_is_reported_and_zero_byte_payload_is_never_sent(self):
        run=subprocess.run(['python3',str(ROOT/'tools/ps5ctl.py'),'doctor'],env=self.environment(),text=True,capture_output=True)
        self.assertEqual(run.returncode,2)
        self.assertIn('PS5_HOST: missing (required)',run.stdout)
        empty=Path(self.scratch.name)/'empty.elf';empty.touch()
        self.config.write_text('PS5_HOST=console.example\n')
        run=subprocess.run(['python3',str(ROOT/'tools/ps5ctl.py'),'payload',str(empty)],env=self.environment(),text=True,capture_output=True)
        self.assertNotEqual(run.returncode,0)
        self.assertIn('expected a nonempty',run.stderr)
        self.assertNotIn('sent ',run.stdout)

    def test_payload_manifest_matches_bundled_bytes_and_licenses(self):
        import json
        root=ROOT/'tools/payloads'
        for entry in json.loads((root/'manifest.json').read_text()).values():
            data=(root/entry['filename']).read_bytes()
            self.assertEqual(len(data),entry['bytes'])
            self.assertEqual(data[:6],b'\x7fELF\x02\x01')
            self.assertEqual(hashlib.sha256(data).hexdigest(),entry['sha256'])
            self.assertTrue((root/entry['license']).is_file())

    def test_screenshot_reports_optional_configuration_missing(self):
        self.config.write_text('PS5_HOST=console.example\n')
        run=subprocess.run(['python3',str(ROOT/'tools/ps5ctl.py'),'shot',str(Path(self.scratch.name)/'frame.jpg')],env=self.environment(),text=True,capture_output=True)
        self.assertEqual(run.returncode,2)
        self.assertIn('set optional P5_MANAGER',run.stderr)
