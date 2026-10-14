# Console media comparison — 2026-10-11

PS5 FW 12.70, test slot <TEST-TITLE-ID>. One initial request and three repeats per model,
then return to Mistral text after each group. No model transfers during generation.
All requests and downloaded artifacts passed. Settings restored to Auto/Auto/Thinking off.

| Model | AGC initial s | Vulkan initial s | AGC repeat median s | Vulkan repeat median s | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| SD-Turbo | 73.482 | 30.045 | 46.048 | 1.001 | 46.025× |
| Stable Audio | 396.044 | 152.796 | 368.103 | 124.106 | 2.966× |
| Kokoro | 88.992 | 89.223 | 84.740 | 85.152 | 0.995× |

These are elapsed HTTP times, including the actual runtime entrypoint. Model-reported
elapsed times are recorded separately. SD repeats retain context; audio/TTS entrypoints
may reload weights. Initial is the first request after switching, not a controlled
cold disk-cache experiment. AGC and Vulkan variants use the same model files/fixtures,
but legacy SD is a prebuilt archive and shared SD is the pinned source build. The
reported speedup compares complete implementations, not an isolated driver/kernel.

## Output validation

- SD: fixed seed 42, one step, CFG 1, 512×512, same prompt. All repeats within each
  backend are identical. Both inspected images show coherent red apples on wood.
  Between backends, RGB mean absolute error 2.754/255, RMSE 5.577/255,
  PSNR 33.203 dB, correlation 0.992512. Outputs are similar, not bit-identical.
- Stable Audio: same prompt/default pipeline seed and latent length 256; stereo PCM16
  44.1 kHz, 524288 frames (11.888617 s), non-silent, no clipped samples.
  Between backends PCM RMSE 7.423/32767, correlation 0.999975.
  Median real-time factor: AGC 30.963, Vulkan 10.439; not real-time synthesis.
- Kokoro: same text, mono PCM16 24 kHz, 52200 frames (2.175 s), non-silent,
  no clipped samples. PCM RMSE 122.600/32767, correlation 0.996637.
  Vulkan repeat median is approximately 0.5% slower. No voice speedup is claimed.
- Numerical similarity and valid files do not establish general perceptual quality.
  Peak memory, sustained thermals and extended switching/leak endurance are unmeasured.

## Runtime decision and next implementation

Keep **one shared Vulkan runtime** as the final target. The SD and Stable Audio
results support migrating those paths. AGC is temporary comparison/compatibility
infrastructure, not a permanent second inference runtime. Full removal remains gated:

1. Build exact Prism Bonsai support and shared SD/audio/Kokoro against one pinned ggml
   ABI. The current working Prism build still uses AGC/private ggml for SD; the shared
   stock media build does not support Prism ternary types. Neither alone proves the target.
2. Profile Kokoro's remaining CPU convolution/LSTM and per-operation bridge allocations
   and uploads. Its functional pass does not pass the faster-than-baseline gate.
3. Measure peak memory and repeat longer text/media switches in the unified build.
4. Migrate legacy AGC text bundles to supported GGUF, resolve remaining provenance and
   Pocket TTS scope, then remove legacy inference/routing/private ggml and scratch.

RADV still uses the platform GPU driver underneath Vulkan; removing our dedicated
AGC inference implementation does not eliminate that driver dependency.

## Reproducible evidence

- The detailed historical JSON receipts and binary artifacts were removed from this PR;
  the measurements above remain as a summary of those comparison runs.
- `tools/benchmark-media-ps5.py` runs the fixed fixtures on a deployed test title;
  `tools/compare-media-benchmarks.py` compares two completed passing records.

Actual images/WAVs are retained under ignored `build/media-compare/{agc,vulkan}`.
The current PR retains the final all-model summary at
[`performance/2026-10-14/pr27-all-models-3x-final.json`](performance/2026-10-14/pr27-all-models-3x-final.json).
PPSA99004 was untouched; no reboot was performed for these comparisons.

## Unified runtime follow-through

The comparison variants above are archived baselines. The current app is named
`vulkan-runtime`, using complete Prism llama plus SD and media on one ABI. Dedicated
AGC inference and private ggml copies are removed. Its functional console receipt is
the retained all-model record; its text benchmark is separate from the earlier figures.
Voice profiling and physical-memory measurement move to
the subsequent draft PR. The known voice tradeoff remains explicitly accepted.
