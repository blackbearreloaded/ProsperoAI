#!/usr/bin/env python3
"""Reproduce a PS5 model bundle from a provenance recipe and source GGUF."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import re
import shutil
import sys
import tempfile
from pathlib import Path

from pack_ps5_model import pack as pack_model
from pack_ps5_model import sha256_file
from pack_tokenizer import pack as pack_tokenizer
from repack_model_from_recipe import MAX_RUNTIME_BYTES, repack


TOOL_NAME = "prosperoai-model-tools"
RECIPE_VERSION = 1
WORKING_MARGIN_BYTES = 512 * 1024 * 1024
MODEL_ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9._-]{0,46}[a-z0-9]")


def valid_sha256(value) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(
        character in "0123456789abcdef" for character in value)


def model_json_bytes(model: dict) -> bytes:
    metadata = {"name": model["name"], "purpose": model["purpose"]}
    return (json.dumps(metadata, indent=2, ensure_ascii=False) +
            "\n").encode("utf-8")


def load_recipe(path: Path) -> dict:
    recipe = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(recipe, dict) or recipe.get("schema_version") != 1:
        raise ValueError("unsupported model-preparation schema")
    tool = recipe.get("tool")
    if not isinstance(tool, dict) or tool.get("name") != TOOL_NAME or \
            tool.get("recipe_version") != RECIPE_VERSION:
        raise ValueError("recipe requires a different model-tools version")

    profile = recipe.get("profile")
    model = recipe.get("model")
    source = recipe.get("source")
    conversion = recipe.get("conversion")
    outputs = recipe.get("outputs")
    if not isinstance(profile, str) or not profile:
        raise ValueError("recipe profile is missing")
    if not all(isinstance(item, dict) for item in
               (model, source, conversion, outputs)):
        raise ValueError("recipe sections are missing")

    model_id = model.get("id")
    name = model.get("name")
    purpose = model.get("purpose")
    if not isinstance(model_id, str) or not MODEL_ID_PATTERN.fullmatch(model_id):
        raise ValueError("recipe model ID is invalid")
    if not isinstance(name, str) or not name.strip() or len(
            name.encode("utf-8")) >= 64 or any(ord(char) < 32 for char in name):
        raise ValueError("recipe model name is invalid")
    if not isinstance(purpose, str) or not purpose.strip() or any(
            ord(char) < 32 for char in purpose):
        raise ValueError("recipe model purpose is invalid")
    creator = recipe.get("creator")
    if not isinstance(creator, str) or not creator.strip() or any(
            ord(char) < 32 for char in creator):
        raise ValueError("recipe creator is invalid")

    if not isinstance(source.get("file"), str) or not source["file"] or \
            not isinstance(source.get("repository"), str) or \
            not isinstance(source.get("revision"), str) or \
            not isinstance(source.get("bytes"), int) or source["bytes"] <= 0 or \
            not valid_sha256(source.get("sha256")):
        raise ValueError("recipe source identity is incomplete")

    packed_bytes = conversion.get("packed_bytes")
    runtime_image = conversion.get("runtime_image")
    if not isinstance(packed_bytes, int) or packed_bytes <= 0 or \
            not isinstance(runtime_image, dict) or \
            not isinstance(runtime_image.get("bytes"), int) or \
            not 256 < runtime_image["bytes"] <= MAX_RUNTIME_BYTES or \
            not isinstance(runtime_image.get("tensors"), list) or \
            len(runtime_image["tensors"]) != conversion.get("tensor_count"):
        raise ValueError("recipe conversion layout is incomplete")

    generated_metadata = model_json_bytes(model)
    expected_sizes = {
        "model.ps5lm": runtime_image["bytes"],
        "model.json": len(generated_metadata),
    }
    for filename in ("model.ps5lm", "tokenizer.ps5tok", "model.json"):
        item = outputs.get(filename)
        if not isinstance(item, dict) or not isinstance(item.get("bytes"), int) \
                or not valid_sha256(item.get("sha256")):
            raise ValueError(f"recipe output identity is missing: {filename}")
        if filename in expected_sizes and item["bytes"] != \
                expected_sizes[filename]:
            raise ValueError(f"recipe output size conflicts: {filename}")
    if hashlib.sha256(generated_metadata).hexdigest() != \
            outputs["model.json"]["sha256"]:
        raise ValueError("recipe model.json identity conflicts with its name")
    return recipe


def file_identity(path: Path) -> dict:
    return {"bytes": path.stat().st_size, "sha256": sha256_file(path)}


def prepare(source_path: Path, provenance_path: Path, output_dir: Path,
            force: bool = False, verify_only: bool = False) -> dict:
    recipe = load_recipe(provenance_path)
    source = recipe["source"]
    if not source_path.is_file() or source_path.suffix.lower() != ".gguf":
        raise ValueError(f"source GGUF does not exist: {source_path}")
    if source_path.stat().st_size != source["bytes"]:
        raise ValueError("source GGUF size does not match the recipe")
    print("[1/5] Verifying source GGUF", flush=True)
    source_sha256 = sha256_file(source_path)
    if source_sha256 != source["sha256"]:
        raise ValueError("source GGUF SHA-256 does not match the recipe")
    if verify_only:
        return {"verified": True, "profile": recipe["profile"],
                "source_sha256": source_sha256}

    models_dir = output_dir / "models"
    models_dir.mkdir(parents=True, exist_ok=True)
    final_dir = models_dir / recipe["model"]["id"]
    if final_dir.exists() and any(final_dir.iterdir()) and not force:
        raise ValueError(f"model folder already exists: {final_dir}")
    required = (recipe["conversion"]["packed_bytes"] +
                recipe["conversion"]["runtime_image"]["bytes"] +
                WORKING_MARGIN_BYTES)
    if final_dir.exists():
        required += sum(path.stat().st_size for path in final_dir.iterdir()
                        if path.is_file())
    free = shutil.disk_usage(models_dir).free
    if free < required:
        raise ValueError(f"not enough free space: need {required / 2**30:.1f} "
                         f"GiB, have {free / 2**30:.1f} GiB")

    with tempfile.TemporaryDirectory(prefix=".prepare-", dir=models_dir) as temp:
        work = Path(temp)
        tokenizer = work / "tokenizer.ps5tok"
        tokenizer_report = work / "tokenizer-report.json"
        print("[2/5] Packing and validating tokenizer", flush=True)
        with contextlib.redirect_stdout(io.StringIO()):
            pack_tokenizer(source_path, tokenizer, tokenizer_report,
                           source_sha256)

        packed = work / "model.packed.ps5lm"
        packed_report = work / "packed-report.json"
        print("[3/5] Packing GGUF tensors", flush=True)
        with contextlib.redirect_stdout(io.StringIO()):
            pack_model(source_path, packed, packed_report, source_sha256,
                       known_source_sha256=source_sha256, hash_output=False)
        if packed.stat().st_size != recipe["conversion"]["packed_bytes"]:
            raise ValueError("packed model size does not match the recipe")

        runtime = work / "model.ps5lm"
        print("[4/5] Applying PS5 runtime layout", flush=True)
        repack(packed, runtime, recipe["conversion"]["runtime_image"],
               recipe["creator"])
        metadata = work / "model.json"
        metadata.write_bytes(model_json_bytes(recipe["model"]))

        print("[5/5] Verifying reproduced outputs", flush=True)
        observed = {
            "model.ps5lm": file_identity(runtime),
            "tokenizer.ps5tok": file_identity(tokenizer),
            "model.json": file_identity(metadata),
        }
        for filename, identity in observed.items():
            if identity != {key: recipe["outputs"][filename][key]
                            for key in ("bytes", "sha256")}:
                raise ValueError(f"reproduced output differs: {filename}")
        tokenizer_details = json.loads(tokenizer_report.read_text("utf-8"))
        expected_checks = recipe["outputs"]["tokenizer.ps5tok"].get(
            "self_checks")
        if expected_checks is not None and tokenizer_details["self_checks"] != \
                expected_checks:
            raise ValueError("tokenizer self-check count differs")

        final_dir.mkdir(parents=True, exist_ok=True)
        runtime.replace(final_dir / "model.ps5lm")
        tokenizer.replace(final_dir / "tokenizer.ps5tok")
        metadata.replace(final_dir / "model.json")
        if force:
            for legacy in ("name.txt", "model-preparation.json",
                           "runtime-layout.json"):
                (final_dir / legacy).unlink(missing_ok=True)

    return {"ready": str(final_dir), "profile": recipe["profile"],
            "outputs": observed}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="raw single-file GGUF")
    parser.add_argument("--provenance", type=Path, required=True,
                        help="model-preparation.json recipe")
    parser.add_argument("-o", "--output-dir", type=Path,
                        default=Path("PPSA99004"),
                        help="ProsperoAI app folder (default: ./PPSA99004)")
    parser.add_argument("--verify-only", action="store_true",
                        help="verify the recipe and source without conversion")
    parser.add_argument("--force", action="store_true",
                        help="replace an existing prepared model after success")
    args = parser.parse_args()
    try:
        result = prepare(args.source.resolve(), args.provenance.resolve(),
                         args.output_dir.resolve(), args.force,
                         args.verify_only)
        print(json.dumps(result, indent=2))
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
