#!/usr/bin/env python3
"""Apply a validated recipe tensor layout to a packed P5LM model."""

from __future__ import annotations

import struct
from pathlib import Path

from repack_ps5_model_runtime import (ENTRY_BYTES, ENTRY_FORMAT, HEADER_BYTES,
                                      HEADER_FORMAT, HEADER_STRUCT_BYTES,
                                      RUNTIME_FLAG, copy_exact,
                                      set_creator_metadata)


MAX_RUNTIME_BYTES = 16 * 1024**3
ZERO_CHUNK = bytes(16 * 1024 * 1024)


def write_zeros(output, size: int) -> None:
    while size:
        chunk = min(size, len(ZERO_CHUNK))
        output.write(ZERO_CHUNK[:chunk])
        size -= chunk


def read_layout(model_path: Path) -> dict:
    with model_path.open("rb") as model:
        header = model.read(HEADER_BYTES)
        if len(header) != HEADER_BYTES:
            raise ValueError("runtime model header is truncated")
        fields = struct.unpack_from(HEADER_FORMAT, header)
        if fields[:4] != (b"P5LM", 1, HEADER_BYTES, ENTRY_BYTES):
            raise ValueError("runtime model is not P5LM v1")
        tensors = []
        for _ in range(fields[4]):
            raw = model.read(ENTRY_BYTES)
            if len(raw) != ENTRY_BYTES:
                raise ValueError("runtime tensor table is truncated")
            values = struct.unpack(ENTRY_FORMAT, raw)
            rank = values[2]
            name = values[0].split(b"\0", 1)[0].decode("utf-8")
            tensors.append({
                "name": name,
                "kind": values[1],
                "dimensions": list(values[3:3 + rank]),
                "offset": values[7],
                "bytes": values[8],
            })
    return {"bytes": model_path.stat().st_size, "tensors": tensors}


def repack(source_path: Path, output_path: Path, runtime_image: dict,
           creator: str) -> None:
    runtime_bytes = runtime_image.get("bytes")
    layout = runtime_image.get("tensors")
    if not isinstance(runtime_bytes, int) or not HEADER_BYTES < runtime_bytes <= \
            MAX_RUNTIME_BYTES:
        raise ValueError("recipe runtime image size is invalid")
    if not isinstance(layout, list) or not layout:
        raise ValueError("recipe runtime tensor layout is empty")
    by_name = {item.get("name"): item for item in layout
               if isinstance(item, dict)}
    if len(by_name) != len(layout) or None in by_name:
        raise ValueError("recipe runtime tensor names are missing or duplicated")

    with source_path.open("rb") as source:
        raw_header = bytearray(source.read(HEADER_BYTES))
        if len(raw_header) != HEADER_BYTES:
            raise ValueError("packed model header is truncated")
        fields = list(struct.unpack_from(HEADER_FORMAT, raw_header))
        if tuple(fields[:4]) != (b"P5LM", 1, HEADER_BYTES, ENTRY_BYTES):
            raise ValueError("packed model is not P5LM v1")

        entries = []
        for _ in range(fields[4]):
            raw_entry = source.read(ENTRY_BYTES)
            if len(raw_entry) != ENTRY_BYTES:
                raise ValueError("packed tensor table is truncated")
            values = list(struct.unpack(ENTRY_FORMAT, raw_entry))
            name = values[0].split(b"\0", 1)[0].decode("utf-8")
            entries.append((name, values, values[7]))
        if len({name for name, _, _ in entries}) != fields[4]:
            raise ValueError("packed model has duplicate tensor names")
        if set(by_name) != {name for name, _, _ in entries}:
            raise ValueError("recipe and packed model tensor sets differ")

        for name, values, _ in entries:
            item = by_name[name]
            rank = values[2]
            expected = (values[1], list(values[3:3 + rank]), values[8])
            actual = (item.get("kind"), item.get("dimensions"),
                      item.get("bytes"))
            if actual != expected:
                raise ValueError(f"recipe tensor metadata differs: {name}")
            offset = item.get("offset")
            if not isinstance(offset, int) or offset % 256:
                raise ValueError(f"recipe tensor offset is invalid: {name}")
            values[7] = offset

        ordered = sorted(entries, key=lambda item: item[1][7])
        table_end = HEADER_BYTES + fields[4] * ENTRY_BYTES
        if ordered[0][1][7] < table_end:
            raise ValueError("recipe tensor data overlaps the P5LM table")
        for (name, values, _), (next_name, next_values, _) in zip(
                ordered, ordered[1:]):
            if values[7] + values[8] > next_values[7]:
                raise ValueError(f"recipe tensors overlap: {name}, {next_name}")
        if ordered[-1][1][7] + ordered[-1][1][8] > runtime_bytes:
            raise ValueError("recipe tensor exceeds the runtime image")

        fields[14] |= RUNTIME_FLAG
        set_creator_metadata(raw_header, creator)
        raw_header[:HEADER_STRUCT_BYTES] = struct.pack(HEADER_FORMAT, *fields)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("wb") as output:
            output.write(raw_header)
            for _, values, _ in entries:
                output.write(struct.pack(ENTRY_FORMAT, *values))
            for _, values, source_offset in ordered:
                write_zeros(output, values[7] - output.tell())
                source.seek(source_offset)
                copy_exact(source, output, values[8])
            write_zeros(output, runtime_bytes - output.tell())
