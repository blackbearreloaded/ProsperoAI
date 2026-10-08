#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare two inference backends behind the same Ollama-style API.

  tools/bench_backend.py --name agc --url http://10.0.0.127:11434 --model qwen35-9b
  tools/bench_backend.py --name llama --url http://10.0.0.127:8080 --model qwen35-9b

Each run sends the same prompt with a fixed output limit and records server-reported
generated tokens, decode time and wall time. Results go to a JSON file so the two backends can
be compared later with --compare. Only the standard library is used.
"""
import argparse
import json
import pathlib
import statistics
import sys
import time
import urllib.request

PROMPT = ("Explain in detail how a GPU executes a matrix multiplication, "
          "covering threads, memory tiers and synchronisation.")


def generate(url, model, prompt, tokens, timeout):
    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"num_predict": tokens, "temperature": 0},
    }).encode()
    request = urllib.request.Request(url.rstrip("/") + "/api/generate", data=body,
                                     headers={"Content-Type": "application/json"})
    started = time.monotonic()
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = json.load(response)
    elapsed = time.monotonic() - started
    if data.get("error"):
        raise RuntimeError(data["error"])
    if "eval_count" not in data or "eval_duration" not in data:
        raise RuntimeError("server did not report token count and decode duration")
    generated = data["eval_count"]
    decode_seconds = data["eval_duration"] / 1e9
    # The first generated token comes from prefill; only subsequent tokens decode.
    decode_tokens = max(generated - 1, 0)
    return {
        "seconds": round(elapsed, 3),
        "generated_tokens": generated,
        "decode_seconds": round(decode_seconds, 6),
        "decode_tokens": decode_tokens,
        "tokens_per_second": round(decode_tokens / decode_seconds, 2) if decode_seconds > 0 else 0.0,
        "wall_tokens_per_second": round(generated / elapsed, 2) if elapsed > 0 else 0.0,
        "prompt_tokens": data.get("prompt_eval_count"),
    }


def run(args):
    runs = []
    for index in range(args.runs):
        try:
            result = generate(args.url, args.model, PROMPT, args.tokens, args.timeout)
        except Exception as error:  # the report must record failures, not hide them
            result = {"error": str(error)}
        result["run"] = index + 1
        runs.append(result)
        print(json.dumps(result), flush=True)
    speeds = [r["tokens_per_second"] for r in runs if "tokens_per_second" in r]
    summary = {
        "backend": args.name,
        "url": args.url,
        "model": args.model,
        "tokens": args.tokens,
        "runs": runs,
        "median_tokens_per_second": round(statistics.median(speeds), 2) if speeds else None,
        "failures": sum(1 for r in runs if "error" in r),
    }
    pathlib.Path(args.out).write_text(json.dumps(summary, indent=2))
    print(f"wrote {args.out}")
    return 1 if summary["failures"] else 0


def compare(paths):
    rows = [json.loads(pathlib.Path(p).read_text()) for p in paths]
    print(f"{'backend':12} {'model':18} {'median tok/s':>13} {'failures':>9}")
    for row in rows:
        median = row["median_tokens_per_second"]
        print(f"{row['backend']:12} {row['model']:18} "
              f"{(f'{median:.2f}' if median is not None else 'n/a'):>13} {row['failures']:>9}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", help="backend label, for example agc or llama")
    parser.add_argument("--url", help="base URL of the server")
    parser.add_argument("--model", help="model id as the server lists it")
    parser.add_argument("--tokens", type=int, default=128)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--out", default="bench-result.json")
    parser.add_argument("--compare", nargs="+", metavar="JSON",
                        help="print a table from result files instead of benchmarking")
    args = parser.parse_args(argv)
    if args.compare:
        return compare(args.compare)
    missing = [name for name in ("name", "url", "model") if not getattr(args, name)]
    if missing:
        parser.error("required: " + ", ".join("--" + m for m in missing))
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
