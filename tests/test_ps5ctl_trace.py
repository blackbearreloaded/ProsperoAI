"""The trace command reads the app debug trace and filters it locally."""
import importlib.util
import io
import os
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class TraceCommand(unittest.TestCase):
    def run_trace(self, **options):
        trace = b'[ 1.0] [app] start\n[ 2.0] [sys] [prosperoai] chatterbox generated\n[ 3.0] [model] idle\n'
        class FTP:
            def retrbinary(self, command, write):
                self.command = command
                write(trace)
            def quit(self):
                pass
        with patch.dict(os.environ, {'PS5_HOST': 'console.invalid'}):
            spec = importlib.util.spec_from_file_location('ps5ctl_trace_test', ROOT / 'tools/ps5ctl.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        ftp = FTP()
        args = SimpleNamespace(prev=False, lines=0, grep='', root='/data/prosperoai')
        for key, value in options.items():
            setattr(args, key, value)
        output = io.StringIO()
        with patch.object(module, 'ftp_connect', return_value=ftp), patch('sys.stdout', output):
            module.cmd_trace(args)
        return ftp.command, output.getvalue()

    def test_current_trace(self):
        command, output = self.run_trace()
        self.assertEqual(command, 'RETR /data/prosperoai/logs/debug-trace.txt')
        self.assertEqual(output.count('\n'), 3)

    def test_previous_trace_filtered(self):
        command, output = self.run_trace(prev=True, grep='chatterbox|idle', lines=1)
        self.assertEqual(command, 'RETR /data/prosperoai/logs/debug-trace.prev.txt')
        self.assertEqual(output, '[ 3.0] [model] idle\n')


if __name__ == '__main__':
    unittest.main()
