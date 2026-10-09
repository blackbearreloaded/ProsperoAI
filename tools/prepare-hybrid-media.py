#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Isolate SD's pinned ggml ABI from llama.cpp's newer Vulkan ggml ABI."""
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/prospero-vulkan-native/media'
out.mkdir(parents=True,exist_ok=True)
archives=['libggml.a','libggml-cpu.a','libggml-base.a']
symbols=set()
for name in archives:
    text=subprocess.check_output(['nm','-g','--defined-only',str(root/'vendor/lib'/name)],text=True)
    for line in text.splitlines():
        fields=line.split()
        if len(fields)!=3:continue
        _,kind,symbol=fields
        if 'ggml' in symbol or 'gguf' in symbol or (kind in 'TDBR' and not symbol.startswith('_')):
            symbols.add(symbol)
rename=out/'symbols.map'
content=''.join(f'{symbol} sd_private_{symbol}\n' for symbol in sorted(symbols))
if not rename.exists() or rename.read_text()!=content:
    rename.write_text(content)
for name in archives+['libstable-diffusion.a']:
    source=root/'vendor/lib'/name;target=out/name
    if not target.exists() or max(source.stat().st_mtime,rename.stat().st_mtime)>target.stat().st_mtime:
        subprocess.run(['llvm-objcopy-18','--redefine-syms='+str(rename),str(source),str(target)],check=True)
