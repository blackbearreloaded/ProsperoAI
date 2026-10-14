#!/usr/bin/env python3
"""Load local console settings without executing shell code or printing secrets."""
from pathlib import Path
import os
import shlex

ROOT = Path(__file__).resolve().parents[1]
KEYS = {"PS5_HOST", "PROSPERO_TEST_TITLE", "PS5_PAYLOAD_PORT", "PS5_FTP_PORT",
        "PS5_AGENT_PORT", "PS5_SHADOW_PORT", "PS5_KLOG_PORT", "PS5_SHSRV_PORT",
        "FTP_PORT", "P5_MANAGER", "P5_SESSION"}

def load_console_env():
    path = Path(os.environ.get("PS5_ENV_FILE") or ROOT / ".env.ps5.local")
    if not path.is_file():
        return
    for number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:]
        key, separator, value = line.partition("=")
        key = key.strip()
        if not separator or key not in KEYS:
            raise ValueError(f"{path}:{number}: unknown console setting")
        values = shlex.split(value, comments=True)
        if len(values) > 1:
            raise ValueError(f"{path}:{number}: quote values containing spaces")
        os.environ.setdefault(key, values[0] if values else "")

if __name__ == "__main__":
    load_console_env()
    for key in sorted(KEYS):
        if key in os.environ:
            print(f"export {key}={shlex.quote(os.environ[key])}")
