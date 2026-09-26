#!/usr/bin/env python3
"""Reorder a packed Mistral 7B image into its final PS5 GPU layout."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


HEADER_BYTES = 256
ENTRY_BYTES = 128
HEADER_FORMAT = "<4s15I32s"
HEADER_STRUCT_BYTES = struct.calcsize(HEADER_FORMAT)
ENTRY_FORMAT = "<80sII4IQQII"
METADATA_FORMAT = "<4sHH"
METADATA_MAGIC = b"P5MD"
METADATA_VERSION = 1
RUNTIME_FLAG = 2
MODEL_BYTES = 0x0E000000
LAYER_STRIDE = 0x08400000
LAYER_NORM_OFFSET = 0x08200000
LAYERS = 32
RUNTIME_BYTES = MODEL_BYTES + LAYERS * LAYER_STRIDE
COPY_BYTES = 16 * 1024 * 1024

GLOBAL_OFFSETS = {
    "output_norm.weight": 0x0000C000,
    "token_embd.weight": 0x00010000,
    "output.weight": 0x05100000,
}
LAYER_OFFSETS = {
    "attn_q.weight": 0x00000000,
    "attn_k.weight": 0x00A00000,
    "attn_v.weight": 0x00C80000,
    "attn_output.weight": 0x00F00000,
    "ffn_gate.weight": 0x01900000,
    "ffn_up.weight": 0x03C00000,
    "ffn_down.weight": 0x05F00000,
    "attn_norm.weight": LAYER_NORM_OFFSET,
    "ffn_norm.weight": LAYER_NORM_OFFSET + 0x4000,
}


def runtime_offset(name: str) -> int:
    if name in GLOBAL_OFFSETS:
        return GLOBAL_OFFSETS[name]
    parts = name.split(".", 2)
    if len(parts) != 3 or parts[0] != "blk":
        raise ValueError(f"unsupported tensor for Mistral 7B layout: {name}")
    layer = int(parts[1])
    if not 0 <= layer < LAYERS or parts[2] not in LAYER_OFFSETS:
        raise ValueError(f"unsupported tensor for Mistral 7B layout: {name}")
    return MODEL_BYTES + layer * LAYER_STRIDE + LAYER_OFFSETS[parts[2]]


def copy_exact(source, output, size: int) -> None:
    while size:
        chunk = source.read(min(size, COPY_BYTES))
        if not chunk:
            raise EOFError("packed model ended before its tensor data")
        output.write(chunk)
        size -= len(chunk)


def set_creator_metadata(header: bytearray, creator: str) -> None:
    creator = creator.strip()
    encoded = creator.encode("utf-8")
    metadata_bytes = struct.calcsize(METADATA_FORMAT) + len(encoded)
    if not creator or any(ord(character) < 32 for character in creator):
        raise ValueError("creator must be one non-empty printable line")
    if metadata_bytes > HEADER_BYTES - HEADER_STRUCT_BYTES:
        raise ValueError("creator is too long for the P5LM metadata block")
    header[HEADER_STRUCT_BYTES:] = bytes(HEADER_BYTES - HEADER_STRUCT_BYTES)
    struct.pack_into(METADATA_FORMAT, header, HEADER_STRUCT_BYTES,
                     METADATA_MAGIC, METADATA_VERSION, len(encoded))
    start = HEADER_STRUCT_BYTES + struct.calcsize(METADATA_FORMAT)
    header[start:start + len(encoded)] = encoded


def get_creator_metadata(header: bytes) -> str | None:
    magic, version, length = struct.unpack_from(
        METADATA_FORMAT, header, HEADER_STRUCT_BYTES)
    if magic != METADATA_MAGIC:
        return None
    if version != METADATA_VERSION:
        raise ValueError(f"unsupported P5LM metadata version: {version}")
    start = HEADER_STRUCT_BYTES + struct.calcsize(METADATA_FORMAT)
    if length > HEADER_BYTES - start:
        raise ValueError("invalid P5LM creator metadata length")
    return header[start:start + length].decode("utf-8")


def repack(source_path: Path, output_path: Path, report_path: Path | None,
           creator: str | None = None) -> None:
    with source_path.open("rb") as source:
        raw_header = bytearray(source.read(HEADER_BYTES))
        if len(raw_header) != HEADER_BYTES:
            raise ValueError("packed model header is truncated")
        fields = list(struct.unpack_from(HEADER_FORMAT, raw_header))
        if fields[0] != b"P5LM" or fields[1] != 1 or fields[2] != HEADER_BYTES \
                or fields[3] != ENTRY_BYTES or fields[4] != 291 \
                or fields[5] != LAYERS:
            raise ValueError("input is not the supported Mistral 7B P5LM image")

        entries = []
        for _ in range(fields[4]):
            values = list(struct.unpack(ENTRY_FORMAT, source.read(ENTRY_BYTES)))
            name = values[0].split(b"\0", 1)[0].decode("utf-8")
            entries.append((name, values, values[7]))
        if len({name for name, _, _ in entries}) != fields[4]:
            raise ValueError("packed model has duplicate tensor names")

        for name, values, _ in entries:
            values[7] = runtime_offset(name)
        ordered = sorted(entries, key=lambda item: item[1][7])
        for (name, values, _), (next_name, next_values, _) in zip(
                ordered, ordered[1:]):
            if values[7] + values[8] > next_values[7]:
                raise ValueError(f"runtime tensors overlap: {name}, {next_name}")
        if ordered[-1][1][7] + ordered[-1][1][8] > RUNTIME_BYTES:
            raise ValueError("runtime tensor exceeds the image")

        fields[14] |= RUNTIME_FLAG
        if creator is not None:
            set_creator_metadata(raw_header, creator)
        raw_header[:HEADER_STRUCT_BYTES] = struct.pack(HEADER_FORMAT, *fields)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("wb") as output:
            output.write(raw_header)
            for _, values, _ in entries:
                output.write(struct.pack(ENTRY_FORMAT, *values))
            for name, values, source_offset in ordered:
                if output.tell() > values[7]:
                    raise ValueError(f"metadata overlaps tensor {name}")
                output.write(bytes(values[7] - output.tell()))
                source.seek(source_offset)
                copy_exact(source, output, values[8])
            output.write(bytes(RUNTIME_BYTES - output.tell()))

    report = {
        "source": str(source_path),
        "source_bytes": source_path.stat().st_size,
        "output": str(output_path),
        "output_bytes": output_path.stat().st_size,
        "layout": "mistral-7b-runtime-v1",
        "tensor_count": len(entries),
        "backward_seeks_at_runtime": 0,
        "creator": get_creator_metadata(raw_header),
    }
    if report_path:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--creator", help="creator stored in the P5LM header")
    args = parser.parse_args()
    repack(args.source, args.output, args.report, args.creator)


if __name__ == "__main__":
    main()
