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
from pack_ps5_model import RUNTIME_LAYOUTS, check_runtime_layout


class RuntimeLayoutGateTest(unittest.TestCase):
    """The packer must reject models the runtime cannot dispatch."""

    def signature(self, layout: str) -> tuple:
        spec = RUNTIME_LAYOUTS[layout]
        return (spec["block_count"], spec["embedding_length"],
                spec["feed_forward_length"], spec["head_count"],
                spec["head_count_kv"], spec["vocab_size"])

    def test_accepts_every_known_layout(self):
        for layout in sorted(RUNTIME_LAYOUTS):
            self.assertEqual(
                check_runtime_layout("test", self.signature(layout)), layout)

    def test_mistral_layout_is_reachable_from_llama_architecture(self):
        # Mistral ships as GGUF architecture "llama", so the gate must key on
        # header dimensions and never on the architecture string alone.
        self.assertEqual(
            check_runtime_layout("llama", self.signature(
                "mistral-7b-runtime-v1")),
            "mistral-7b-runtime-v1")

    def test_rejects_unsupported_model_with_clear_message(self):
        with self.assertRaises(ValueError) as raised:
            check_runtime_layout("gemma", (26, 2048, 8192, 8, 4, 256000))
        message = str(raised.exception)
        self.assertIn("gemma", message)
        self.assertIn("mistral-7b-runtime-v1", message)
        self.assertIn("qwen35-9b-runtime-v1", message)

    def test_rejects_known_architecture_with_wrong_dimensions(self):
        # A real trap: a Qwen GGUF whose vocab size differs has no layout.
        with self.assertRaises(ValueError) as raised:
            check_runtime_layout("qwen35", (32, 4096, 12288, 16, 4, 151936))
        self.assertIn("qwen35", str(raised.exception))


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
