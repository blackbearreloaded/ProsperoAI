#!/usr/bin/env python3
"""Combine graphics SDK exports with the inference-only AGC import declaration."""
from pathlib import Path
import re
import subprocess
import sys

sdk_stub, output = map(Path, sys.argv[1:])
symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(sdk_stub)], text=True)
names = {line.split()[-1] for line in symbols.splitlines()
         if re.search(r"\bFUNC\s+GLOBAL\s+DEFAULT\s+\d+\s+sceAgc\w+$", line)}
if "sceAgcInit" not in names:
    raise SystemExit("graphics SDK does not export sceAgcInit")
names.add("sceAgcDcbWaitRegMem")
# Import declarations only: the .so is a link/conversion input, never packaged.
source = "/* Generated import declarations, never executed. */\n"
source += "".join(f"void {name}(void) {{}}\n" for name in sorted(names))
output.parent.mkdir(parents=True, exist_ok=True)
if not output.exists() or output.read_text() != source:
    output.write_text(source)
