#!/usr/bin/env python3
"""Small self-check for recipe capture and validation."""

import json
import struct
import tempfile
import unittest
from pathlib import Path

from capture_model_recipe import capture, file_identity
from prepare_model import load_recipe
from repack_ps5_model_runtime import (ENTRY_BYTES, ENTRY_FORMAT, HEADER_BYTES,
                                      HEADER_FORMAT)


class ModelToolsTest(unittest.TestCase):
    def test_capture_creates_a_valid_recipe(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.gguf"
            packed = root / "packed.ps5lm"
            runtime = root / "model.ps5lm"
            tokenizer = root / "tokenizer.ps5tok"
            seed = root / "seed.json"
            recipe_path = root / "recipe.json"
            source.write_bytes(b"GGUF test source")
            packed.write_bytes(b"packed model")
            tokenizer.write_bytes(b"P5TK test tokenizer")

            header_values = [1, HEADER_BYTES, ENTRY_BYTES, 1] + [1] * 8 + [
                HEADER_BYTES + ENTRY_BYTES, 1, 1]
            header = struct.pack(HEADER_FORMAT, b"P5LM", *header_values,
                                 bytes(32))
            entry = struct.pack(
                ENTRY_FORMAT, b"token_embd.weight", 0, 1, 1, 0, 0, 0,
                HEADER_BYTES + ENTRY_BYTES, 4, 0, 0)
            runtime.write_bytes(header + bytes(HEADER_BYTES - len(header)) +
                                entry + b"data")
            seed.write_text(json.dumps({
                "profile": "prosperoai-test-v1",
                "model": {"purpose": "text-to-text"},
                "creator": "TestCreator",
                "source": {
                    "repository": "owner/model",
                    "revision": "0123456789abcdef",
                },
                "conversion": {
                    "architecture": "test",
                    "runtime_layout": "test-v1",
                },
            }), encoding="utf-8")

            capture(seed, source, packed, runtime, tokenizer, "test-model",
                    "Test Model", recipe_path)
            recipe = load_recipe(recipe_path)
            self.assertEqual(recipe["model"]["purpose"], "text-to-text")
            self.assertEqual(recipe["source"]["sha256"],
                             file_identity(source)["sha256"])
            self.assertEqual(recipe["outputs"]["model.ps5lm"],
                             file_identity(runtime))
            self.assertEqual(recipe["outputs"]["tokenizer.ps5tok"],
                             file_identity(tokenizer))


if __name__ == "__main__":
    unittest.main()
