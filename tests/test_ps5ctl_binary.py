"""Console screenshot downloads must preserve arbitrary binary bytes."""
import importlib.util
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class BinaryDownload(unittest.TestCase):
    def test_binary_download(self):
        payload = b'BM\x00\xff\x80\xfe\r\n\x00image'
        class FTP:
            closed = False
            def retrbinary(self, command, write):
                self.command = command
                write(payload[:5])
                write(payload[5:])
            def quit(self):
                self.closed = True
        with patch.dict(os.environ, {'PS5_HOST': 'console.invalid'}):
            spec = importlib.util.spec_from_file_location('ps5ctl_binary_test', ROOT / 'tools/ps5ctl.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        ftp = FTP()
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'capture/image.bmp'
            with patch.object(module, 'ftp_connect', return_value=ftp):
                module.cmd_ftp(SimpleNamespace(action='get', remote='/download0/image.bmp', local=str(target)))
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(ftp.command, 'RETR /download0/image.bmp')
            self.assertTrue(ftp.closed)
