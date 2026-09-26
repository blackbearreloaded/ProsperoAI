#!/usr/bin/env python3
"""Add a complete runtime tensor layout to a model provenance recipe."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from repack_model_from_recipe import read_layout


TOOL = {
    "name": "prosperoai-model-tools",
    "recipe_version": 1,
    "repository":
        "https://github.com/blackbearreloaded/ProsperoAI/tree/main/model-tools",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_identity(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def capture(provenance: Path, source_model: Path, packed_model: Path,
            runtime_model: Path, tokenizer: Path, model_id: str,
            display_name: str, output: Path) -> None:
    original = json.loads(provenance.read_text(encoding="utf-8"))
    model = dict(original.get("model", {}))
    model.update({"id": model_id, "name": display_name})
    model_json = (json.dumps({"name": display_name,
                              "purpose": model["purpose"]}, indent=2,
                             ensure_ascii=False) + "\n").encode("utf-8")
    runtime_image = read_layout(runtime_model)
    conversion = dict(original.get("conversion", {}))
    conversion["packed_bytes"] = packed_model.stat().st_size
    conversion["runtime_image"] = runtime_image
    conversion["tensor_count"] = len(runtime_image["tensors"])
    outputs = dict(original.get("outputs", {}))
    tokenizer_output = dict(outputs.get("tokenizer.ps5tok", {}))
    tokenizer_output.update(file_identity(tokenizer))
    outputs["model.ps5lm"] = file_identity(runtime_model)
    outputs["tokenizer.ps5tok"] = tokenizer_output
    outputs["model.json"] = {
        "bytes": len(model_json),
        "sha256": sha256_bytes(model_json),
    }
    source = dict(original["source"])
    source.update({"file": source_model.name, **file_identity(source_model)})
    recipe = {
        "schema_version": 1,
        "tool": TOOL,
        "profile": original["profile"],
        "model": model,
        "creator": original["creator"],
        "source": source,
        "conversion": conversion,
        "outputs": outputs,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("provenance", type=Path)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--packed-model", type=Path, required=True)
    parser.add_argument("--runtime-model", type=Path, required=True)
    parser.add_argument("--tokenizer", type=Path, required=True)
    parser.add_argument("--model-id", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    capture(args.provenance, args.source, args.packed_model,
            args.runtime_model, args.tokenizer, args.model_id, args.name,
            args.output)


if __name__ == "__main__":
    main()
