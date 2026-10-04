#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Download GGUF models from Hugging Face, only if they fit the memory budget.

For every GGUF file in a repository the tool reads the header (HTTP Range requests, no full
download), estimates weights + KV cache for the chosen context and KV type, and downloads the
file only when the estimate fits the budget. Downloads are verified against the LFS SHA-256.

  tools/hf_fetch.py list REPO [--budget-gib 12] [--ctx 8192] [--kv q8_0]
  tools/hf_fetch.py get REPO FILENAME [--budget-gib 12] [--ctx 8192] [--kv q8_0] [--dest models]

The estimate is conservative for dense models with grouped-query attention. Models with
sliding-window or hybrid attention may need less KV memory than estimated; models with
unusual KV layouts are reported as unknown instead of guessed.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import sys
import urllib.error
import urllib.request

API = "https://huggingface.co/api"
RESOLVE = "https://huggingface.co/{repo}/resolve/main/{name}"
GIB = 1024 ** 3
OVERHEAD_GIB = 0.5  # compute buffers and runtime, on top of weights and KV
BYTES_PER_ELEMENT = {"f16": 2.0, "f32": 4.0, "q8_0": 34 / 32, "q4_0": 18 / 32}

# GGUF metadata value types (ggml's gguf.h)
SCALAR = {0: ("B", 1), 1: ("b", 1), 2: ("H", 2), 3: ("h", 2), 4: ("I", 4), 5: ("i", 4),
          6: ("f", 4), 7: ("B", 1), 10: ("Q", 8), 11: ("q", 8), 12: ("d", 8)}


def request(url, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    return urllib.request.urlopen(req, timeout=60)


def list_gguf(repo):
    with request(f"{API}/models/{repo}/tree/main") as response:
        entries = json.load(response)
    files = []
    for entry in entries:
        name = entry.get("path", "")
        if not name.endswith(".gguf") or "mmproj" in name.lower():
            continue
        lfs = entry.get("lfs") or {}
        files.append({"name": name, "size": lfs.get("size", entry.get("size", 0)),
                      "sha256": lfs.get("oid")})
    return files


def read_header(repo, name, limit=32 * 1024 * 1024):
    """Return the GGUF key/value metadata (tensor data is not read)."""
    url = RESOLVE.format(repo=repo, name=name)
    with request(url, headers={"Range": f"bytes=0-{limit - 1}"}) as response:
        data = response.read()
    reader = Reader(data)
    if reader.take(4) != b"GGUF":
        raise ValueError("not a GGUF file")
    version = reader.unpack("I")
    if version < 2:
        raise ValueError(f"unsupported GGUF version {version}")
    reader.unpack("Q")  # tensor count
    count = reader.unpack("Q")
    metadata = {}
    for _ in range(count):
        key = reader.string()
        value_type = reader.unpack("I")
        metadata[key] = reader.value(value_type, keep=wanted(key))
    return metadata


WANTED_SUFFIXES = (".block_count", ".attention.head_count", ".attention.head_count_kv",
                   ".embedding_length", ".attention.key_length")


def wanted(key):
    return key == "general.architecture" or key.endswith(WANTED_SUFFIXES)


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def take(self, size):
        if self.at + size > len(self.data):
            raise ValueError("header larger than the range read; metadata is unusually big")
        chunk = self.data[self.at:self.at + size]
        self.at += size
        return chunk

    def unpack(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))[0]

    def string(self):
        return self.take(self.unpack("Q")).decode("utf-8", "replace")

    def value(self, value_type, keep=False):
        if value_type in SCALAR:
            fmt, size = SCALAR[value_type]
            raw = self.take(size)
            return struct.unpack("<" + fmt, raw)[0] if keep else None
        if value_type == 8:
            text = self.string()
            return text if keep else None
        if value_type == 9:
            element = self.unpack("I")
            count = self.unpack("Q")
            if element == 8:
                for _ in range(count):
                    self.string()
            elif element in SCALAR:
                self.take(SCALAR[element][1] * count)
            else:
                raise ValueError(f"unknown array element type {element}")
            return None
        raise ValueError(f"unknown metadata type {value_type}")


def estimate(metadata, size_bytes, ctx, kv_type):
    arch = metadata.get("general.architecture")
    if not arch:
        return None
    try:
        layers = int(metadata[f"{arch}.block_count"])
        heads = int(metadata[f"{arch}.attention.head_count"])
        kv_heads = int(metadata.get(f"{arch}.attention.head_count_kv", heads))
        width = int(metadata[f"{arch}.embedding_length"])
        head_dim = int(metadata.get(f"{arch}.attention.key_length", width // heads))
    except (KeyError, ValueError, TypeError):
        return None
    per_token = 2 * layers * kv_heads * head_dim * BYTES_PER_ELEMENT[kv_type]
    kv = per_token * ctx
    total = size_bytes + kv + OVERHEAD_GIB * GIB
    return {"weights": size_bytes, "kv": kv, "total": total, "arch": arch,
            "layers": layers, "kv_heads": kv_heads, "head_dim": head_dim}


def fits(est, budget):
    return est is not None and est["total"] <= budget * GIB


def fmt_gib(value):
    return f"{value / GIB:.2f} GiB"


def cmd_list(args):
    rows = []
    for item in list_gguf(args.repo):
        try:
            metadata = read_header(args.repo, item["name"])
        except (ValueError, urllib.error.URLError) as error:
            rows.append((item["name"], item["size"], None, f"header unreadable: {error}"))
            continue
        est = estimate(metadata, item["size"], args.ctx, args.kv)
        if est is None:
            rows.append((item["name"], item["size"], None, "unknown architecture metadata"))
        else:
            verdict = "fits" if fits(est, args.budget_gib) else "too big"
            rows.append((item["name"], item["size"], est, verdict))
    for name, size, est, verdict in rows:
        total = fmt_gib(est["total"]) if est else "-"
        print(f"{verdict:12} {total:>10}  weights {fmt_gib(size):>10}  {name}")
    return 0


def cmd_get(args):
    files = {item["name"]: item for item in list_gguf(args.repo)}
    item = files.get(args.filename)
    if item is None:
        print(f"{args.filename} is not a GGUF file of {args.repo}", file=sys.stderr)
        return 2
    metadata = read_header(args.repo, item["name"])
    est = estimate(metadata, item["size"], args.ctx, args.kv)
    if est is None:
        print("cannot estimate memory for this model; refusing to download", file=sys.stderr)
        return 2
    if not fits(est, args.budget_gib):
        print(f"does not fit: needs {fmt_gib(est['total'])}, budget {args.budget_gib} GiB",
              file=sys.stderr)
        return 2
    dest = pathlib.Path(args.dest) / args.repo.replace("/", "--")
    dest.mkdir(parents=True, exist_ok=True)
    target = dest / item["name"]
    if target.exists() and target.stat().st_size == item["size"]:
        print(f"already present: {target}")
        return 0
    print(f"needs {fmt_gib(est['total'])} of the {args.budget_gib} GiB budget; downloading "
          f"{fmt_gib(item['size'])} to {target}")
    digest = hashlib.sha256()
    with request(RESOLVE.format(repo=args.repo, name=item["name"])) as response, \
            open(target, "wb") as out:
        done = 0
        while chunk := response.read(8 * 1024 * 1024):
            out.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            print(f"\r  {fmt_gib(done)} / {fmt_gib(item['size'])}", end="", flush=True)
    print()
    if item["sha256"] and digest.hexdigest() != item["sha256"]:
        target.unlink()
        print("SHA-256 mismatch; file removed", file=sys.stderr)
        return 1
    print(f"verified: {target}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("list", "get"):
        p = sub.add_parser(name)
        p.add_argument("repo")
        if name == "get":
            p.add_argument("filename")
            p.add_argument("--dest", default="models")
        p.add_argument("--budget-gib", type=float, default=12.0)
        p.add_argument("--ctx", type=int, default=8192)
        p.add_argument("--kv", choices=sorted(BYTES_PER_ELEMENT), default="q8_0")
    args = parser.parse_args(argv)
    return cmd_list(args) if args.command == "list" else cmd_get(args)


if __name__ == "__main__":
    sys.exit(main())
