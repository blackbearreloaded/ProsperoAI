#!/usr/bin/env python3
"""Combine the pinned ORT static components into one PS5-linkable object."""
from __future__ import annotations

import pathlib
import re
import shlex
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <ort-link.txt> <output.o>", file=sys.stderr)
        return 2
    link_file = pathlib.Path(sys.argv[1]).resolve()
    output = pathlib.Path(sys.argv[2]).resolve()
    build_dir = link_file.parents[2]
    args = shlex.split(link_file.read_text())
    inputs = [item for item in args if item.endswith((".o", ".a"))]
    if not inputs or not any(item.endswith("generated_source.c.o") for item in inputs):
        raise SystemExit(f"unexpected ORT link inputs in {link_file}")
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ld.lld-18", "-r", "-u", "OrtGetApiBase", "-o", str(output), *inputs],
        cwd=build_dir,
        check=True,
    )
    defined = subprocess.check_output(
        ["nm", "-g", "--defined-only", "--format=posix", str(output)], text=True
    )
    # This object is embedded in a much larger app with its own libc++ and other
    # runtimes. Export only the ORT C API entry points used by adapters; otherwise
    # weak C++/TLS helpers from ORT can bind across runtime boundaries.
    public_symbols = {"OrtGetApiBase", "OrtSessionOptionsAppendExecutionProvider_CPU"}
    private_symbols = [
        line.split()[0]
        for line in defined.splitlines()
        if line.split() and line.split()[0] not in public_symbols
    ]
    symbol_file = None
    try:
        with tempfile.NamedTemporaryFile("w", prefix="ort-localize-", suffix=".txt",
                                         dir=output.parent, delete=False) as stream:
            symbol_file = pathlib.Path(stream.name)
            stream.write("\n".join(private_symbols) + "\n")
        if private_symbols:
            subprocess.run(
                ["llvm-objcopy-18", f"--localize-symbols={symbol_file}", str(output)],
                check=True,
            )
    finally:
        if symbol_file:
            symbol_file.unlink(missing_ok=True)
    info = subprocess.check_output(["readelf", "-h", str(output)], text=True)
    if "Type:" not in info or "REL (Relocatable file)" not in info:
        raise SystemExit(f"ORT output is not a relocatable ELF object: {output}")
    symbols = subprocess.check_output(["nm", "-g", "--defined-only", str(output)], text=True)
    if " OrtGetApiBase" not in symbols or "OrtSessionOptionsAppendExecutionProvider_CPU" not in symbols:
        raise SystemExit("combined ORT object is missing a public ONNX Runtime C API entry point")
    unexpected = [
        line for line in symbols.splitlines()
        if line.split() and line.split()[-1] not in public_symbols
    ]
    if unexpected:
        raise SystemExit(f"unexpected public ORT symbols remain: {unexpected[:5]}")
    print(
        f"Built {output} ({output.stat().st_size} bytes); "
        f"localized {len(private_symbols)} ORT implementation symbols; "
        f"retained {len(public_symbols)} C API entry points"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
