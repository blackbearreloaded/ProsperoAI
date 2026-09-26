#!/usr/bin/env python3
"""Pack a GGUF into a directly loadable, PS5-compute-friendly model image."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

try:
    import numpy as np
except ModuleNotFoundError:
    np = None

from extract_gguf_q4_0 import parse, tensor_bytes


HEADER_BYTES = 256
ENTRY_BYTES = 128
DATA_ALIGNMENT = 0x4000
TENSOR_ALIGNMENT = 256
SEGMENT_BYTES = 0x2000000
KIND_F32 = 0
KIND_Q4_0_F32 = 1
KIND_Q8_0_F32 = 2
KIND_Q6_K_F32 = 3
KIND_Q4_1_F32 = 4
KIND_Q5_K_F32 = 5


def align(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def tensor_offset(cursor: int, size: int) -> int:
    cursor = align(cursor, TENSOR_ALIGNMENT)
    if size <= SEGMENT_BYTES and \
            cursor // SEGMENT_BYTES != \
            (cursor + size - 1) // SEGMENT_BYTES:
        cursor = align(cursor, SEGMENT_BYTES)
    return cursor


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def expanded_bytes(tensor) -> int:
    source_bytes = tensor_bytes(tensor)
    if tensor["type"] == 0:
        return source_bytes
    if tensor["type"] == 2:
        return source_bytes // 18 * 20
    if tensor["type"] == 3:
        return source_bytes // 20 * 24
    if tensor["type"] == 8:
        return source_bytes // 34 * 36
    if tensor["type"] == 13:
        return source_bytes // 176 * 224
    if tensor["type"] == 14:
        return source_bytes // 210 * 256
    raise ValueError(f"unsupported tensor type {tensor['type']}")


def expanded_kind(source_type: int) -> int:
    return {
        0: KIND_F32,
        2: KIND_Q4_0_F32,
        3: KIND_Q4_1_F32,
        8: KIND_Q8_0_F32,
        13: KIND_Q5_K_F32,
        14: KIND_Q6_K_F32,
    }[source_type]


def write_expanded(output, raw: bytes, source_type: int):
    if source_type == 0:
        output.write(raw)
        return
    if np is not None:
        if source_type == 13:
            blocks = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 176)
            packed = blocks[:, 4:16]
            scales = np.empty((len(blocks), 8), dtype=np.uint8)
            mins = np.empty((len(blocks), 8), dtype=np.uint8)
            scales[:, :4] = packed[:, :4] & 63
            mins[:, :4] = packed[:, 4:8] & 63
            scales[:, 4:] = ((packed[:, 8:12] & 15) |
                              ((packed[:, :4] >> 6) << 4))
            mins[:, 4:] = ((packed[:, 8:12] >> 4) |
                            ((packed[:, 4:8] >> 6) << 4))
            d = blocks[:, :2].copy().view("<f2").astype("<f4")
            dmin = blocks[:, 2:4].copy().view("<f2").astype("<f4")
            output.write((scales.astype("<f4") * d).tobytes())
            output.write((mins.astype("<f4") * dmin).tobytes())
            output.write(blocks[:, 16:].tobytes())
            return
        if source_type == 14:
            blocks = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 210)
            scales = blocks[:, 208:210].copy().view("<f2").astype("<f4")
            subscales = blocks[:, 192:208].view(np.int8).astype("<f4")
            output.write((subscales * scales).astype("<f4").tobytes())
            output.write(blocks[:, :192].tobytes())
            return
        if source_type == 3:
            blocks = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 20)
            output.write(blocks[:, :2].copy().view("<f2").astype("<f4")
                         .tobytes())
            output.write(blocks[:, 2:4].copy().view("<f2").astype("<f4")
                         .tobytes())
            output.write(blocks[:, 4:].tobytes())
            return
        block_bytes = 18 if source_type == 2 else 34
        blocks = np.frombuffer(raw, dtype=np.uint8).reshape(-1, block_bytes)
        output.write(blocks[:, :2].copy().view("<f2").astype("<f4").tobytes())
        output.write(blocks[:, 2:].tobytes())
        return
    if source_type == 13:
        blocks = len(raw) // 176
        scale_values = []
        min_values = []
        for block in range(blocks):
            at = block * 176
            d, dmin = struct.unpack_from("<ee", raw, at)
            packed = raw[at + 4:at + 16]
            for index in range(8):
                if index < 4:
                    scale = packed[index] & 63
                    minimum = packed[index + 4] & 63
                else:
                    scale = ((packed[index + 4] & 15) |
                             ((packed[index - 4] >> 6) << 4))
                    minimum = ((packed[index + 4] >> 4) |
                               ((packed[index] >> 6) << 4))
                scale_values.append(d * scale)
                min_values.append(dmin * minimum)
        output.write(struct.pack(f"<{len(scale_values)}f", *scale_values))
        output.write(struct.pack(f"<{len(min_values)}f", *min_values))
        for block in range(blocks):
            at = block * 176
            output.write(raw[at + 16:at + 176])
        return
    if source_type == 14:
        blocks = len(raw) // 210
        for block in range(blocks):
            at = block * 210
            scale = struct.unpack_from("<e", raw, at + 208)[0]
            for subscale in struct.unpack_from("<16b", raw, at + 192):
                output.write(struct.pack("<f", scale * subscale))
        for block in range(blocks):
            at = block * 210
            output.write(raw[at:at + 192])
        return
    if source_type == 3:
        blocks = len(raw) // 20
        for block in range(blocks):
            output.write(struct.pack("<f", struct.unpack_from(
                "<e", raw, block * 20)[0]))
        for block in range(blocks):
            output.write(struct.pack("<f", struct.unpack_from(
                "<e", raw, block * 20 + 2)[0]))
        for block in range(blocks):
            at = block * 20 + 4
            output.write(raw[at:at + 16])
        return
    block_bytes = 18 if source_type == 2 else 34
    blocks = len(raw) // block_bytes
    for block in range(blocks):
        scale = struct.unpack_from("<e", raw, block * block_bytes)[0]
        output.write(struct.pack("<f", scale))
    payload_bytes = block_bytes - 2
    for block in range(blocks):
        at = block * block_bytes + 2
        output.write(raw[at:at + payload_bytes])


def pack(model: Path, output_path: Path, report_path: Path,
         expected_sha256: str | None, known_source_sha256: str | None = None,
         hash_output: bool = True):
    handle, data, version, alignment, data_offset, metadata, tensors = \
        parse(model)
    try:
        architecture = metadata["general.architecture"]
        model_key = architecture + "."
        embedding = next(tensor for tensor in tensors
                         if tensor["name"] == "token_embd.weight")
        vocab_size = int(metadata.get(model_key + "vocab_size",
                                      embedding["dimensions"][1]))
        model_sha256 = known_source_sha256 or hashlib.sha256(data).hexdigest()
        if expected_sha256 and model_sha256 != expected_sha256:
            raise ValueError(f"model SHA-256 is {model_sha256}, expected "
                             f"{expected_sha256}")
        output_data_offset = align(HEADER_BYTES + len(tensors) * ENTRY_BYTES,
                                   DATA_ALIGNMENT)
        entries = []
        cursor = output_data_offset
        for tensor in tensors:
            size = expanded_bytes(tensor)
            cursor = tensor_offset(cursor, size)
            entries.append(dict(tensor, output_offset=cursor,
                                output_bytes=size,
                                kind=expanded_kind(tensor["type"])))
            cursor += size

        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("wb") as output:
            header = struct.pack(
                "<4s15I32s", b"P5LM", 1, HEADER_BYTES, ENTRY_BYTES,
                len(tensors), int(metadata[model_key + "block_count"]),
                int(metadata[model_key + "context_length"]),
                int(metadata[model_key + "embedding_length"]),
                int(metadata[model_key + "feed_forward_length"]),
                int(metadata[model_key + "attention.head_count"]),
                int(metadata[model_key + "attention.head_count_kv"]),
                vocab_size,
                int(metadata[model_key + "rope.dimension_count"]),
                output_data_offset, 1, SEGMENT_BYTES,
                bytes.fromhex(model_sha256))
            output.write(header)
            output.write(bytes(HEADER_BYTES - len(header)))
            for entry in entries:
                name = entry["name"].encode("utf-8")
                if len(name) >= 80 or len(entry["dimensions"]) > 4:
                    raise ValueError(f"tensor name/dimensions too large: "
                                     f"{entry['name']}")
                dimensions = entry["dimensions"] + [0] * (
                    4 - len(entry["dimensions"]))
                output.write(struct.pack(
                    "<80sII4IQQII", name, entry["kind"],
                    len(entry["dimensions"]), *dimensions,
                    entry["output_offset"], entry["output_bytes"],
                    entry["type"], 0))
            output.write(bytes(output_data_offset - output.tell()))
            for tensor, entry in zip(tensors, entries):
                output.write(bytes(entry["output_offset"] - output.tell()))
                source_at = data_offset + tensor["offset"]
                raw = data[source_at:source_at + tensor_bytes(tensor)]
                write_expanded(output, raw, tensor["type"])
                if output.tell() != entry["output_offset"] + \
                        entry["output_bytes"]:
                    raise AssertionError(f"size mismatch for {tensor['name']}")
            output.write(bytes(align(output.tell(), DATA_ALIGNMENT) -
                               output.tell()))

        pack_sha256 = sha256_file(output_path) if hash_output else None
        report = {
            "model": str(model),
            "architecture": architecture,
            "model_sha256": model_sha256,
            "gguf_version": version,
            "gguf_alignment": alignment,
            "output": str(output_path),
            "output_sha256": pack_sha256,
            "output_bytes": output_path.stat().st_size,
            "data_offset": output_data_offset,
            "segment_bytes": SEGMENT_BYTES,
            "segment_count": align(output_path.stat().st_size,
                                   SEGMENT_BYTES) // SEGMENT_BYTES,
            "tensor_count": len(tensors),
            "kinds": {
                "f32": sum(entry["kind"] == KIND_F32 for entry in entries),
                "q4_0_f32": sum(entry["kind"] == KIND_Q4_0_F32
                                for entry in entries),
                "q4_1_f32": sum(entry["kind"] == KIND_Q4_1_F32
                                for entry in entries),
                "q8_0_f32": sum(entry["kind"] == KIND_Q8_0_F32
                                 for entry in entries),
                "q5_k_f32": sum(entry["kind"] == KIND_Q5_K_F32
                                for entry in entries),
                "q6_k_f32": sum(entry["kind"] == KIND_Q6_K_F32
                                 for entry in entries),
            },
        }
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
    finally:
        data.close()
        handle.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--expect-sha256")
    args = parser.parse_args()
    pack(args.model, args.output, args.report, args.expect_sha256)


if __name__ == "__main__":
    main()
