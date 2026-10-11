# Ternary Bonsai 2 27B on PS5

This is the current top priority. Test the exact checkpoint selected by the user,
`prism-ml/Ternary-Bonsai-2-27B-gguf`, rather than extrapolating the older Bonsai results.

## Pinned inputs

- Model revision: `b072e1d3b35a0a630cece372c2127528e0994386`.
- Compact pack: `Ternary-Bonsai-2-27B-PTQ1_0.gguf`, 5946648928 bytes,
  SHA256 `53107f530aa52eb00912263ab1ee29bd199261c87cd7b4ad4ca1318c1fe33ee3`.
- Prism runtime: `e8fd209c975c80aa7fb36a8423571ff3b1b7e955`, release
  `prism-b10772-e8fd209`, source archive hash pinned in `tools/deps.json`.
- GGUF architecture is `qwen35`; tensor types are F32, BF16 and private PTQ1_0 (143).
  PTQ1_0 groups have 128 elements in 28 packed bytes. PQ2_0 is private type 142,
  128 elements in 34 bytes. These are not aliases for stock Q2_0/TQ1_0.

Use the fork's low-bit and Hadamard operations together. Changing a tensor type ID
or importing only its dequantizer does not establish correct inference. The stock
runtime is not used to load this checkpoint. The production build now uses `vulkan-runtime`: one shared Prism/SD ggml ABI,
Vulkan text and media, without AGC inference or private ggml copies. The archived
comparison records below describe earlier experimental builds.

## Build

```sh
bash tools/build-bonsai-runtime.sh host
PROSPERO_TEST_TITLE=<TEST-TITLE-ID> make app-bonsai-folder
```

App output is `build/vulkan-runtime/<TEST-TITLE-ID>`. These commands do not deploy.
Update the folder only after stopping the test title and verifying `mounted:false`.
Never update PPSA99004 for this experiment.

## Unified runtime results — 2026-10-11

The final common-ABI console build generates 94 identical tokens in each of three
warm greedy runs: **11.202328 tokens/s median** (11.204053 / 11.200721 / 11.202328).
Thinking off/on, 4K/16K, Mistral, MiniCPM5, Qwen 3.5 and return to Bonsai pass.
Evidence: `performance/2026-10-11/vulkan-runtime-bonsai.json`.

AirLLM/Strata bounded residency is a separate follow-up draft PR, not implemented
streamed inference in this runtime.

## Archived comparison results — 2026-10-11

Native inference passes on PS5 FW 12.70 in folder-backed test title <TEST-TITLE-ID>.
Model host SHA256 and full console readback match the pinned digest. The final
Prism/media build preserves entry 0x120, 14 program headers and the same 15 NEEDED
libraries as the normal build. Exact build and model receipts are in
`performance/2026-10-11/bonsai2-{build,model-verification}.json`.

| Resident Vulkan test | Result |
| --- | --- |
| Decode, three warm repeats | **11.208 tokens/s median**, range 11.207–11.209 |
| Tokens generated per repeat | 94 / 94 / 94; identical greedy responses |
| End-to-end time per repeat | 9.115 / 9.119 / 9.123 seconds |
| Cold one-word response | 5.634 seconds including model load |
| Thinking off/on | Clean answer / closed reasoning block plus correct 2/5 answer |
| Context recreation | 4K → 16K, correct output, weights retained |
| Existing GGUF models | Mistral 7B Q4_0, MiniCPM5 Q8_0, Qwen3.5 9B Q4_0 answer correctly |
| Model return | Bonsai → old models → Bonsai passes |
| Legacy/media switching | AGC Mistral → Kokoro → Bonsai passes; valid non-silent mono 24 kHz WAV |

Method: temperature zero, saved 4K context, automatic KV (f16 at 4K), resident
weights, 256-token output allowance, no console model transfer during timing.
Decode rate is Ollama `eval_count * 1e9 / eval_duration`; prompt processing is
excluded. The 27-token prompt still gets full prefill on this hybrid model.
These are smoke tests and a short repeatable speed measurement, not a general
quality benchmark or proof of 64K/128K context, all 4096 output tokens, or sustained
thermal performance. Reproduce on an already deployed test app:

```sh
PS5_HOST=<PS5-IP> python3 tools/benchmark-bonsai-ps5.py --include-qwen \
  --output build/bonsai2-benchmark.json
```

The optional console preset is pinned to the exact repository revision, file size
and SHA256 above. Its ID equals the verified installed GGUF filename. It is included
only with `PROSPERO_PRISM_RUNTIME`; stock/shared builds omit it because they cannot
interpret private types 142/143. Existing preset indices and default models stay
unchanged. Downloads use the existing resume/checksum pipeline.

The chat formatter supplies Qwen's missing `enable_thinking` suffix and includes
an already-prompted opening marker in the displayed reasoning response without
counting it as a generated token. Wrapped or otherwise invalid Vulkan free-memory
reports select a conservative 4K Auto context, instead of treating unsigned
underflow as available RAM.

The final Kokoro request took 85.630 seconds and produced 52200 PCM frames; this
is functional evidence, not a perceptual-parity or speed-improvement claim.
SD-Turbo and Stable Audio are now installed: all 81 files passed full console SHA256
readback (`performance/2026-10-11/bonsai2-media-install-verified.json`). The Prism
smoke produced a coherent 512×512 apple image in 73.085 seconds and a non-silent
stereo 44.1 kHz WAV with 524288 frames in 152.424 seconds; return to Bonsai passed.
These initial requests include loading. SD still uses AGC in this build. Shared
Vulkan parity/performance and one common Prism/media ABI remain open.
Evidence: `performance/2026-10-11/media-prism-smoke.json`.

Host checks: 54 tests complete with one opt-in skipped, lint passes, separate actual
Prism cache checks pass under ASan/UBSan. The Prism Vulkan media numerical test also
passes on llvmpipe (30 Vulkan calls, zero fallback calls). The new unified runtime includes the Bonsai preset by default.

Native AirLLM/Strata streaming is still S0 only: extent indexing and bounded disk
measurement. The resident baseline above now permits S1 output equivalence checks;
no native streamed inference is claimed.

## Sources

- [Checkpoint and required fork](https://huggingface.co/prism-ml/Ternary-Bonsai-2-27B-gguf/blob/b072e1d3b35a0a630cece372c2127528e0994386/README.md).
- [Prism backend support](https://github.com/PrismML-Eng/Bonsai-demo/blob/main/BACKEND-SUPPORT.md).
- [Pinned Vulkan backend](https://github.com/PrismML-Eng/llama.cpp/blob/e8fd209c975c80aa7fb36a8423571ff3b1b7e955/ggml/src/ggml-vulkan/ggml-vulkan.cpp).
