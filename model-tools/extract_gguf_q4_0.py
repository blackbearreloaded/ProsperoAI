#!/usr/bin/env python3
"""Extract one GGUF Q4_0 matrix into the PS5-friendly scale/nibble layout."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import mmap
import struct
from pathlib import Path


SCALARS = {
    0: "B", 1: "b", 2: "H", 3: "h", 4: "I", 5: "i", 6: "f",
    7: "?", 10: "Q", 11: "q", 12: "d",
}


class Reader:
    def __init__(self, data: mmap.mmap):
        self.data = data
        self.at = 0

    def scalar(self, fmt: str):
        size = struct.calcsize("<" + fmt)
        value = struct.unpack_from("<" + fmt, self.data, self.at)[0]
        self.at += size
        return value

    def string(self) -> str:
        size = self.scalar("Q")
        value = self.data[self.at:self.at + size].decode("utf-8")
        self.at += size
        return value

    def value(self, kind: int, keep: bool = False):
        if kind in SCALARS:
            value = self.scalar(SCALARS[kind])
            return value if keep else None
        if kind == 8:
            value = self.string()
            return value if keep else None
        if kind == 9:
            item_kind = self.scalar("I")
            count = self.scalar("Q")
            values = [] if keep else None
            for _ in range(count):
                value = self.value(item_kind, keep)
                if keep:
                    values.append(value)
            return values
        raise ValueError(f"unsupported GGUF metadata type {kind}")


TYPE_NAMES = {
    0: "F32",
    2: "Q4_0",
    3: "Q4_1",
    8: "Q8_0",
    13: "Q5_K",
    14: "Q6_K",
}


def tensor_bytes(tensor):
    elements = math.prod(tensor["dimensions"])
    if tensor["type"] == 0:
        return elements * 4
    if tensor["type"] == 2:
        return elements // 32 * 18
    if tensor["type"] == 3:
        return elements // 32 * 20
    if tensor["type"] == 8:
        return elements // 32 * 34
    if tensor["type"] == 13:
        return elements // 256 * 176
    if tensor["type"] == 14:
        return elements // 256 * 210
    raise ValueError(f"unsupported GGML tensor type {tensor['type']}")


def parse(path: Path,
          # The empty prefix matches every key, so parse() now retains all
          # GGUF metadata, not just the architecture's. That is deliberate:
          # pack_ps5_model needs block_count and friends for an architecture it
          # may not know in order to report "no runtime layout" instead of
          # raising KeyError. The cost is that unrelated metadata such as the
          # large tokenizer.ggml.* arrays is also held in memory during packing.
          metadata_prefixes=("general.", "llama.", "mistral.", "qwen35.",
                             "smollm3.", "")):
    handle = path.open("rb")
    data = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
    reader = Reader(data)
    if data[:4] != b"GGUF":
        raise ValueError("not a GGUF file")
    reader.at = 4
    version = reader.scalar("I")
    tensor_count = reader.scalar("Q")
    metadata_count = reader.scalar("Q")
    alignment = 32
    metadata = {}
    for _ in range(metadata_count):
        key = reader.string()
        kind = reader.scalar("I")
        keep = key.startswith(metadata_prefixes)
        value = reader.value(kind, keep)
        if keep:
            metadata[key] = value
        if key == "general.alignment":
            alignment = int(value)
    tensors = []
    for _ in range(tensor_count):
        name = reader.string()
        dimensions = [reader.scalar("Q") for _ in range(reader.scalar("I"))]
        tensors.append({
            "name": name,
            "dimensions": dimensions,
            "type": reader.scalar("I"),
            "offset": reader.scalar("Q"),
        })
    data_offset = (reader.at + alignment - 1) // alignment * alignment
    return handle, data, version, alignment, data_offset, metadata, tensors


def emit_u32(output, name: str, values):
    output.write(f"static const uint32_t {name}[] = {{\n")
    for at in range(0, len(values), 8):
        words = ", ".join(f"UINT32_C(0x{value:08x})" for value in values[at:at + 8])
        output.write(f"    {words},\n")
    output.write("};\n\n")


def extract(path: Path, tensor_name: str, output_path: Path, report_path: Path):
    handle, data, version, alignment, data_offset, metadata, tensors = parse(path)
    try:
        tensor = next(item for item in tensors if item["name"] == tensor_name)
        if tensor["type"] != 2 or len(tensor["dimensions"]) != 2:
            raise ValueError("selected tensor must be a two-dimensional Q4_0 matrix")
        columns, rows = tensor["dimensions"]
        if columns % 32:
            raise ValueError("Q4_0 column count is not divisible by 32")
        block_count = rows * columns // 32
        raw_bytes = block_count * 18
        start = data_offset + tensor["offset"]
        raw = data[start:start + raw_bytes]
        if len(raw) != raw_bytes:
            raise ValueError("tensor extends past end of GGUF")

        scale_words = []
        q4_words = []
        expected = []
        activations = [((column % 7) - 3) * 4 for column in range(columns)]
        for block in range(block_count):
            at = block * 18
            scale = struct.unpack_from("<e", raw, at)[0]
            scale_words.append(struct.unpack("<I", struct.pack("<f", scale))[0])
            q4_words.extend(struct.unpack_from("<4I", raw, at + 2))
        for row in range(rows):
            total = 0.0
            for row_block in range(columns // 32):
                block = row * (columns // 32) + row_block
                at = block * 18
                scale = struct.unpack_from("<e", raw, at)[0]
                block_sum = 0
                for byte_index, packed in enumerate(raw[at + 2:at + 18]):
                    block_sum += ((packed & 15) - 8) * activations[row_block * 32 + byte_index]
                    block_sum += ((packed >> 4) - 8) * activations[row_block * 32 + byte_index + 16]
                total += block_sum * scale * 0.25
            expected.append(struct.unpack("<I", struct.pack("<f", total))[0])

        packed_input = []
        for at in range(0, columns, 4):
            packed_input.append(sum((activations[at + byte] & 255) << (byte * 8)
                                    for byte in range(4)))
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", newline="\n") as output:
            output.write("/* Generated from a real GGUF Q4_0 tensor. */\n")
            emit_u32(output, "real_tensor_packed_input", packed_input)
            emit_u32(output, "real_tensor_weights", scale_words + q4_words)
            emit_u32(output, "real_tensor_expected_bits", expected)
        report = {
            "model": str(path),
            "model_sha256": hashlib.sha256(data).hexdigest(),
            "gguf_version": version,
            "alignment": alignment,
            "metadata": metadata,
            "tensor": tensor_name,
            "columns": columns,
            "rows": rows,
            "blocks": block_count,
            "raw_bytes": raw_bytes,
            "expanded_bytes": (len(scale_words) + len(q4_words)) * 4,
        }
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    finally:
        data.close()
        handle.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("--tensor")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--all", action="store_true",
                        help="list tensors of every GGML type")
    parser.add_argument("--manifest", type=Path,
                        help="write the complete tensor map as JSON")
    args = parser.parse_args()
    if not args.tensor:
        handle, data, version, alignment, data_offset, metadata, tensors = \
            parse(args.model)
        try:
            if args.manifest:
                manifest = {
                    "model": str(args.model),
                    "model_sha256": hashlib.sha256(data).hexdigest(),
                    "gguf_version": version,
                    "alignment": alignment,
                    "data_offset": data_offset,
                    "metadata": metadata,
                    "tensors": [dict(tensor,
                                     type_name=TYPE_NAMES.get(
                                         tensor["type"], "UNKNOWN"),
                                     bytes=tensor_bytes(tensor))
                                for tensor in tensors],
                }
                args.manifest.parent.mkdir(parents=True, exist_ok=True)
                args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
                print(json.dumps({"manifest": str(args.manifest),
                                  "tensors": len(tensors)}, indent=2))
                return
            for tensor in tensors:
                if args.all or (tensor["type"] == 2 and
                                len(tensor["dimensions"]) == 2):
                    print(tensor["name"], *tensor["dimensions"],
                          TYPE_NAMES.get(tensor["type"],
                                         f"type={tensor['type']}"))
        finally:
            data.close()
            handle.close()
        return
    if not args.output or not args.report:
        parser.error("--output and --report are required with --tensor")
    extract(args.model, args.tensor, args.output, args.report)


if __name__ == "__main__":
    main()
