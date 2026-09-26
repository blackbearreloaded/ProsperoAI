# ProsperoAI model preparation tools

These scripts reproduce a PS5-ready ProsperoAI text-model folder from a raw,
single-file GGUF and a reviewed `model-preparation.json` recipe. They use only
Python's standard library; NumPy is optional but strongly recommended for
practical conversion speed.

No model weights are stored in this repository.

## Requirements

- 64-bit Python 3.10 or newer.
- Enough free space for the source, packed intermediate, and final image.
  Mistral needs about 9.2 GiB of conversion space; Qwen needs about 13.2 GiB.
- NumPy (`python3 -m pip install numpy`) for faster tensor conversion.

## Reproduce a validated model

Clone ProsperoAI and enter this directory:

```bash
git clone https://github.com/blackbearreloaded/ProsperoAI.git
cd ProsperoAI/model-tools
```

Download the exact GGUF recorded by one of the included recipes. With the
Hugging Face CLI:

```bash
hf download QuantFactory/Mistral-7B-Instruct-v0.3-GGUF \
  Mistral-7B-Instruct-v0.3.Q4_0.gguf \
  --revision a83fbb7c79c1afd59dfcce75272591cbe5a2412f \
  --local-dir sources/mistral

hf download unsloth/Qwen3.5-9B-GGUF Qwen3.5-9B-Q4_0.gguf \
  --revision 3885219b6810b007914f3a7950a8d1b469d598a5 \
  --local-dir sources/qwen
```

Convert either model into an extracted ProsperoAI app folder:

```bash
./prepare-model \
  --provenance recipes/mistral-7b-instruct-v0.3-q4-0.json \
  sources/mistral/Mistral-7B-Instruct-v0.3.Q4_0.gguf \
  --output-dir /path/to/PPSA99004

./prepare-model \
  --provenance recipes/qwen3.5-9b-q4-0.json \
  sources/qwen/Qwen3.5-9B-Q4_0.gguf \
  --output-dir /path/to/PPSA99004
```

Use `python3 prepare_model.py` instead of `./prepare-model` on systems that do
not preserve executable permissions. `--verify-only` checks the source without
writing multi-gigabyte outputs. `--force` replaces an existing prepared folder
only after the new conversion passes every check.

The result is directly installable:

```text
PPSA99004/models/<model-id>/
|-- model.ps5lm
|-- tokenizer.ps5tok
`-- model.json
```

The converter verifies the source byte count and SHA-256, packs every tensor
and tokenizer table, applies the recorded GPU runtime layout, and verifies all
three generated files against their expected sizes and SHA-256 hashes. A
failure leaves the destination untouched.

## What a recipe contains

Schema version 1 records:

- the exact upstream repository, filename, revision, size, and SHA-256;
- the ProsperoAI backend profile, model ID, friendly name, and creator;
- GGUF architecture and packing details;
- every tensor's name, type, dimensions, size, and final GPU offset;
- final runtime-image size; and
- expected sizes and hashes for every generated file.

The checked-in recipes are the exact records published with the curated
[Mistral](https://huggingface.co/blackbearreloaded/ProsperoAI-Mistral-7B-Instruct-v0.3-Q4_0-PS5)
and [Qwen](https://huggingface.co/blackbearreloaded/ProsperoAI-Qwen3.5-9B-Q4_0-PS5)
bundles.

## Port a new text model

`prepare-model` is architecture-neutral once a valid recipe and matching app
backend exist. A recipe does not create GPU inference code. Porting a new
architecture therefore has two distinct stages:

1. Add or validate its runtime profile in ProsperoAI. The backend must define
   the accepted architecture, tensor names and dimensions, GPU layout, context
   limits, tokenizer behavior, and inference implementation.
2. Produce and test one known-good runtime image on a PS5.
3. Record that result as a deterministic recipe so everyone else can reproduce
   it from the original GGUF.

Create the generic packed model and tokenizer first:

```bash
python3 pack_ps5_model.py source.gguf work/model.packed.ps5lm \
  --report work/packed-report.json --expect-sha256 SOURCE_SHA256

python3 pack_tokenizer.py source.gguf work/tokenizer.ps5tok \
  --report work/tokenizer-report.json
```

The current packer supports F32, Q4_0, Q4_1, Q8_0, Q5_K, and Q6_K tensors.
The tokenizer packer supports the SentencePiece and GPT-2 BPE variants already
implemented by ProsperoAI, including the Qwen3.5 pre-tokenizer. New primitives
must be implemented in both these tools and the app.

Next, arrange the packed tensors into the exact layout consumed by the new
backend and validate that runtime image on the console. The included
`repack_ps5_model_runtime.py` is specifically the Mistral 7B layout; do not use
it for another architecture. `repack_model_from_recipe.py` replays an existing
layout but does not invent one.

Once the runtime image works, copy `recipe-seed.example.json`, fill in its
profile, creator, upstream repository and revision, architecture, and layout
name, then capture the complete recipe:

```bash
python3 capture_model_recipe.py recipe-seed.json \
  --source source.gguf \
  --packed-model work/model.packed.ps5lm \
  --runtime-model work/model.ps5lm \
  --tokenizer work/tokenizer.ps5tok \
  --model-id your-model-id \
  --name "Your Model Name" \
  --output recipes/your-model.json
```

The capture command derives the source and output hashes, sizes, tensor table,
and model metadata. Review the resulting JSON, then prove it from scratch with
`prepare-model`. Publish the recipe beside the converted model with the
upstream license and model card.

## Script map

| Script | Purpose |
| --- | --- |
| `prepare-model` / `prepare_model.py` | Verify a recipe and reproduce an installable bundle |
| `pack_ps5_model.py` | Convert supported GGUF tensors into packed P5LM form |
| `pack_tokenizer.py` | Build the native P5TK tokenizer |
| `repack_model_from_recipe.py` | Apply a recorded runtime tensor layout |
| `capture_model_recipe.py` | Capture a proven runtime image as a complete recipe |
| `repack_ps5_model_runtime.py` | Build the fixed Mistral 7B runtime layout |
| `extract_gguf_q4_0.py` | Inspect or extract GGUF tensor data during backend work |

## Safety and limits

- Never trust a filename alone; recipes pin byte counts and SHA-256 hashes.
- Existing output folders are preserved unless `--force` is explicit.
- Temporary conversion output stays beside the destination and is removed on
  failure.
- The runtime image limit is 16 GiB.
- Only use weights and tokenizers under terms permitted by their upstream
  licenses.
