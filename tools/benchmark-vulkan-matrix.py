#!/usr/bin/env python3
"""Benchmark PS5 Vulkan decode across models, contexts and batch sizes.

The server is expected to expose ProsperoAI's Ollama-compatible /api/generate.
Each matrix cell performs one unreported warmup followed by measured repetitions.
Model IDs can point at different GGUF quantizations of the same architecture.
"""

import argparse
import itertools
import json
import pathlib
import statistics
import time
import urllib.request

PROMPT = ("Explain how a GPU executes quantized matrix multiplication, including "
          "workgroups, cache use and synchronization.")


def positive_csv(value, minimum=1, maximum=None):
    parsed = [int(item) for item in value.split(",") if item]
    if not parsed or any(item < minimum or (maximum and item > maximum) for item in parsed):
        raise argparse.ArgumentTypeError(f"expected comma-separated integers in {minimum}..{maximum or 'inf'}")
    return parsed


def request(url, model, context, batch, ubatch, tokens, timeout):
    payload = {
        "model": model,
        "prompt": PROMPT,
        "stream": False,
        "options": {
            "temperature": 0,
            "num_predict": tokens,
            "num_ctx": context,
            "num_batch": batch,
            "num_ubatch": ubatch,
        },
    }
    req = urllib.request.Request(
        url.rstrip("/") + "/api/generate", json.dumps(payload).encode(),
        {"Content-Type": "application/json"})
    started = time.monotonic()
    with urllib.request.urlopen(req, timeout=timeout) as response:
        result = json.load(response)
    wall = time.monotonic() - started
    if result.get("error"):
        raise RuntimeError(result["error"])
    generated = result.get("eval_count", 0)
    decode_ns = result.get("eval_duration", 0)
    decode_tokens = max(generated - 1, 0)
    return {
        "wall_seconds": round(wall, 6),
        "prompt_tokens": result.get("prompt_eval_count"),
        "generated_tokens": generated,
        "decode_seconds": round(decode_ns / 1e9, 6),
        "decode_tokens_per_second": round(decode_tokens * 1e9 / decode_ns, 4)
        if decode_ns else 0.0,
    }


def summarize(runs):
    passed = [run for run in runs if "error" not in run]
    speeds = [run["decode_tokens_per_second"] for run in passed]
    return {
        "passed": len(passed),
        "failed": len(runs) - len(passed),
        "median_decode_tokens_per_second": round(statistics.median(speeds), 4) if speeds else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--models", required=True, type=lambda v: [x for x in v.split(",") if x])
    parser.add_argument("--contexts", default="4096,8192,16384",
                        type=lambda v: positive_csv(v, 512, 262144))
    parser.add_argument("--batches", default="128,256,512",
                        type=lambda v: positive_csv(v, 32, 2048))
    parser.add_argument("--ubatches", default="128,256",
                        type=lambda v: positive_csv(v, 32, 2048))
    parser.add_argument("--tokens", default=128, type=int)
    parser.add_argument("--runs", default=3, type=int)
    parser.add_argument("--timeout", default=900, type=float)
    parser.add_argument("--out", default="vulkan-matrix.json")
    args = parser.parse_args()

    cells = []
    for model, context, batch, ubatch in itertools.product(
            args.models, args.contexts, args.batches, args.ubatches):
        if ubatch > batch:
            continue
        cell = {"model": model, "context": context, "batch": batch, "ubatch": ubatch}
        try:
            request(args.url, model, context, batch, ubatch, min(args.tokens, 16), args.timeout)
        except Exception as error:
            cell["warmup_error"] = str(error)
        runs = []
        for index in range(args.runs):
            try:
                run = request(args.url, model, context, batch, ubatch, args.tokens, args.timeout)
            except Exception as error:
                run = {"error": str(error)}
            run["run"] = index + 1
            runs.append(run)
        cell["runs"] = runs
        cell["summary"] = summarize(runs)
        cells.append(cell)
        print(json.dumps({key: cell[key] for key in ("model", "context", "batch", "ubatch", "summary")}), flush=True)

    report = {
        "schema": 1,
        "prompt": PROMPT,
        "tokens": args.tokens,
        "runs_per_cell": args.runs,
        "cells": cells,
    }
    pathlib.Path(args.out).write_text(json.dumps(report, indent=2) + "\n")
    return 1 if any(cell["summary"]["failed"] for cell in cells) else 0


if __name__ == "__main__":
    raise SystemExit(main())
