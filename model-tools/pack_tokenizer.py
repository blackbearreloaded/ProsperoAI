#!/usr/bin/env python3
"""Pack a GGUF GPT-2 BPE tokenizer for the native PS5 runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import unicodedata
from pathlib import Path

from extract_gguf_q4_0 import parse


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def byte_maps():
    values = list(range(ord("!"), ord("~") + 1))
    values += list(range(0xA1, 0xAD)) + list(range(0xAE, 0x100))
    encoded = values[:]
    extra = 0
    for value in range(256):
        if value not in values:
            values.append(value)
            encoded.append(256 + extra)
            extra += 1
    encoder = dict(zip(values, map(chr, encoded)))
    return encoder, {character: value for value, character in encoder.items()}


def pack_spm(source: Path, output: Path, report: Path, metadata,
             known_source_sha256: str | None = None) -> None:
    vocabulary = metadata["tokenizer.ggml.tokens"]
    scores = metadata["tokenizer.ggml.scores"]
    token_types = metadata["tokenizer.ggml.token_type"]
    if len(vocabulary) != len(scores) or len(vocabulary) != len(token_types):
        raise ValueError("SPM tokenizer arrays have different lengths")

    token_ids = {token: index for index, token in enumerate(vocabulary)}
    unknown = int(metadata.get("tokenizer.ggml.unknown_token_id") or 0)
    byte_tokens = [unknown] * 256
    for token_id, (token, token_type) in enumerate(zip(vocabulary,
                                                       token_types)):
        if token_type == 6 and len(token) == 6 and token.startswith("<0x"):
            byte_tokens[int(token[3:5], 16)] = token_id

    encoded_tokens = [token.encode("utf-8") for token in vocabulary]
    offsets = [0]
    token_bytes = bytearray()
    for token in encoded_tokens:
        token_bytes.extend(token)
        offsets.append(len(token_bytes))
    token_order = sorted(range(len(vocabulary)),
                         key=lambda token_id: encoded_tokens[token_id])

    def encode(text: str):
        symbols = list((" " + text).replace(" ", "▁"))
        while len(symbols) > 1:
            best = None
            for index in range(len(symbols) - 1):
                token_id = token_ids.get(symbols[index] + symbols[index + 1])
                if token_id is None:
                    continue
                candidate = (scores[token_id], -index, index)
                if best is None or candidate > best:
                    best = candidate
            if best is None:
                break
            index = best[2]
            symbols[index:index + 2] = [symbols[index] + symbols[index + 1]]
        result = []
        for symbol in symbols:
            token_id = token_ids.get(symbol)
            if token_id is not None:
                result.append(token_id)
            else:
                result.extend(byte_tokens[value]
                              for value in symbol.encode("utf-8"))
        return result

    checks = {
        "Hello, we can use": [23325, 29493, 1246, 1309, 1706],
        "The capital of France is": [1183, 6333, 1070, 5611, 1117],
        "Hello world! 123": [23325, 2294, 29576, 29473, 29508, 29518, 29538],
        "Café déjà vu.": [1102, 2783, 29565, 29088, 21388, 29491],
        "  multiple   spaces": [1027, 5934, 1027, 11367],
    }
    for text, expected in checks.items():
        actual = encode(text)
        if actual != expected:
            raise ValueError(f"SPM tokenizer self-check failed for {text!r}: "
                             f"{actual}")

    payload = bytearray(struct.pack(
        "<4sIIII", b"P5TK", 2, len(vocabulary), 0, len(token_bytes)))
    payload.extend(struct.pack("<256I", *byte_tokens))
    payload.extend(struct.pack(f"<{len(offsets)}I", *offsets))
    payload.extend(token_bytes)
    while len(payload) & 3:
        payload.append(0)
    payload.extend(struct.pack(f"<{len(scores)}f", *scores))
    payload.extend(bytes(token_types))
    while len(payload) & 3:
        payload.append(0)
    payload.extend(struct.pack(f"<{len(token_order)}I", *token_order))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)

    summary = {
        "source": str(source),
        "source_sha256": known_source_sha256 or sha256_file(source),
        "output": str(output),
        "output_sha256": hashlib.sha256(payload).hexdigest(),
        "output_bytes": len(payload),
        "vocab_size": len(vocabulary),
        "algorithm": "sentencepiece-bpe",
        "token_bytes": len(token_bytes),
        "self_checks": len(checks),
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


def pack(source: Path, output: Path, report: Path,
         known_source_sha256: str | None = None) -> None:
    handle, data, _, _, _, metadata, _ = parse(source, ("tokenizer.",))
    try:
        tokenizer_model = metadata.get("tokenizer.ggml.model")
        tokenizer_pre = metadata.get("tokenizer.ggml.pre")
        if tokenizer_model == "gpt2":
            if tokenizer_pre not in ("smollm", "smaug-bpe", "qwen35"):
                raise ValueError("expected a supported GPT-2 tokenizer")
            vocabulary = metadata["tokenizer.ggml.tokens"]
            merges = metadata["tokenizer.ggml.merges"]
        elif tokenizer_model != "llama":
            raise ValueError(f"unsupported tokenizer model: {tokenizer_model!r}")
    finally:
        data.close()
        handle.close()

    if tokenizer_model == "llama":
        pack_spm(source, output, report, metadata, known_source_sha256)
        return

    encoder, decoder = byte_maps()
    token_ids = {token: index for index, token in enumerate(vocabulary)}
    unknown = int(metadata.get("tokenizer.ggml.unknown_token_id") or 0)
    byte_tokens = [token_ids.get(encoder[value], unknown) for value in range(256)]
    decoded = [bytes(decoder[character] for character in token)
               for token in vocabulary]
    offsets = [0]
    token_bytes = bytearray()
    for token in decoded:
        token_bytes.extend(token)
        offsets.append(len(token_bytes))

    merge_map = {}
    merge_entries = []
    for rank, rule in enumerate(merges):
        left, right = rule.split(" ", 1)
        entry = (token_ids[left], token_ids[right], token_ids[left + right], rank)
        merge_map[entry[:2]] = entry[2:]
        merge_entries.append(entry)
    merge_entries.sort(key=lambda entry: (entry[0], entry[1]))

    def split_qwen35(text: str):
        def letter_mark(character: str) -> bool:
            return unicodedata.category(character)[:1] in ("L", "M")

        def number(character: str) -> bool:
            return unicodedata.category(character)[:1] == "N"

        pieces = []
        at = 0
        while at < len(text):
            start = at
            lowered = text[at:at + 3].lower()
            contraction = next((item for item in
                                ("'re", "'ve", "'ll", "'s", "'t", "'m", "'d")
                                if lowered.startswith(item)), None)
            if contraction:
                at += len(contraction)
            elif (text[at] not in "\r\n" and not number(text[at]) and
                  (letter_mark(text[at]) or
                   (at + 1 < len(text) and letter_mark(text[at + 1])))):
                at += 1
                while at < len(text) and letter_mark(text[at]):
                    at += 1
            elif number(text[at]):
                at += 1
            else:
                punct = at + 1 if text[at] == " " else at
                if (punct < len(text) and not text[punct].isspace() and
                        not letter_mark(text[punct]) and
                        not number(text[punct])):
                    at = punct + 1
                    while (at < len(text) and not text[at].isspace() and
                           not letter_mark(text[at]) and
                           not number(text[at])):
                        at += 1
                    while at < len(text) and text[at] in "\r\n":
                        at += 1
                elif text[at].isspace():
                    end = at
                    last_newline = 0
                    while end < len(text) and text[end].isspace():
                        if text[end] in "\r\n":
                            last_newline = end + 1
                        end += 1
                    if last_newline:
                        at = last_newline
                    elif end - at > 1 and end < len(text):
                        at = end - 1
                    else:
                        at = end
                else:
                    at += 1
            pieces.append(text[start:at])
        return pieces

    def split_pieces(text: str):
        if tokenizer_pre == "qwen35":
            return split_qwen35(text)
        pieces = []
        at = 0
        contractions = ("'s", "'t", "'re", "'ve", "'m", "'ll", "'d")
        while at < len(text):
            suffix = next((item for item in contractions
                           if text.startswith(item, at)), None)
            if suffix:
                pieces.append(suffix)
                at += len(suffix)
                continue
            if text[at].isdigit():
                width = 3 if tokenizer_pre == "smaug-bpe" else 1
                start = at
                while at < len(text) and text[at].isdigit() and \
                        at - start < width:
                    at += 1
                pieces.append(text[start:at])
                continue
            start = at
            if text[at].isspace():
                while at < len(text) and text[at].isspace():
                    at += 1
                if at == len(text):
                    pieces.append(text[start:at])
                    break
                if text[at].isdigit():
                    pieces.append(text[start:at])
                    continue
                if at - start > 1:
                    pieces.append(text[start:at - 1])
                start = at - 1
            letters = text[at].isalpha()
            while at < len(text) and not text[at].isspace() and \
                    not text[at].isdigit() and text[at].isalpha() == letters:
                at += 1
            pieces.append(text[start:at])
        return pieces

    def encode_piece(piece: str):
        symbols = [byte_tokens[value] for value in piece.encode("utf-8")]
        while len(symbols) > 1:
            best = None
            for index in range(len(symbols) - 1):
                found = merge_map.get((symbols[index], symbols[index + 1]))
                if found is not None and (best is None or found[1] < best[0]):
                    best = (found[1], index, found[0])
            if best is None:
                break
            symbols[best[1]:best[1] + 2] = [best[2]]
        return symbols

    def encode(text: str):
        return [token for piece in split_pieces(text)
                for token in encode_piece(piece)]

    checks = {
        "Hello, we can use": [19556, 28, 392, 416, 722],
        "The capital of France is": [504, 3575, 282, 4649, 314],
        "You are a helpful AI assistant named SmolLM, trained by Hugging Face":
            [2683, 359, 253, 5356, 5646, 11173, 3365, 3511, 308, 34519,
             28, 7018, 411, 407, 19712, 8182],
        "\n": [198],
        "Hello world! 123": [19556, 905, 17, 216, 33, 34, 35],
        "Café déjà vu.": [51, 1939, 2756, 32564, 90, 16739, 386, 101, 30],
        "  multiple   spaces": [216, 2701, 256, 5600],
    } if tokenizer_pre == "smollm" else ({
        "Hello, we can use": [9906, 11, 584, 649, 1005],
    } if tokenizer_pre == "smaug-bpe" else {
        "Hello, we can use": [9419, 11, 567, 628, 958],
        "The capital of France is": [760, 6511, 314, 9338, 369],
        "Hello world! 123": [9419, 1814, 0, 220, 16, 17, 18],
        "  multiple   spaces": [220, 5081, 256, 12258],
        "user\nHello": [846, 198, 9419],
        "assistant\n": [74455, 198],
        "\n\n": [271],
    })
    for text, expected in checks.items():
        actual = encode(text)
        if actual != expected:
            raise ValueError(f"tokenizer self-check failed for {text!r}: {actual}")

    payload = bytearray(struct.pack(
        "<4sIIII", b"P5TK", 3 if tokenizer_pre == "qwen35" else 1,
        len(vocabulary), len(merge_entries),
        len(token_bytes)))
    payload.extend(struct.pack("<256I", *byte_tokens))
    payload.extend(struct.pack(f"<{len(offsets)}I", *offsets))
    payload.extend(token_bytes)
    while len(payload) & 3:
        payload.append(0)
    for entry in merge_entries:
        payload.extend(struct.pack("<IIII", *entry))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)

    summary = {
        "source": str(source),
        "source_sha256": known_source_sha256 or sha256_file(source),
        "output": str(output),
        "output_sha256": hashlib.sha256(payload).hexdigest(),
        "output_bytes": len(payload),
        "vocab_size": len(vocabulary),
        "pre_tokenizer": tokenizer_pre,
        "merge_count": len(merge_entries),
        "token_bytes": len(token_bytes),
        "self_checks": len(checks),
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    pack(args.source, args.output, args.report)


if __name__ == "__main__":
    main()
