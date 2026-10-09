# Active work

Updated: 2026-10-09. Plan: [VK_PLAN.md](VK_PLAN.md); dated evidence: [VK_LOG.md](VK_LOG.md).

## Current console build

- Folder test title PPSA99023 runs the native UI rendered with Vulkan. The earlier
  OpenGL/RADV link blocker is superseded by this Vulkan presentation path.
- Text inference uses the current Vulkan llama.cpp backend; Image, Audio and Voice
  link the existing AGC media backend in the same binary. SD's old ggml ABI is isolated
  with renamed archive/object symbols. Backend switching releases resident allocations.
- Kokoro 82M FP16 downloaded on console from its pinned manifest, passed size/SHA-256
  verification and became visible in the combined catalog. HTTPS redirect handling and
  writes through the sandbox's mounted model directory are verified.
- Actual HTTP Voice generation succeeded: mono PCM16 WAV, 24000 Hz, 32400 frames
  (1.35 s), peak 9847, 19502 nonzero samples. Then Mistral Vulkan text succeeded in
  the same process: “Hello there! How can I assist you today?” (10 output tokens).
- Catalog/HTTP metadata stays responsive during generation. Separate catalog and inference
  mutexes replace the recursive-lock path that stalled earlier Voice requests.
- Image and Audio generation in this combined build are linked but not console-tested.
  Do not claim those generation paths are verified from Voice success.

## UI and downloads

- Matching icon/home background and a quiet system launch backdrop; native splash lettering
  moved lower. User confirmed the new graphics after their console restart.
- Models categories use L2/R2; arrows select cards. Search keyboard uses Triangle Space,
  R2 Done. Contextual search hints replace ambiguous Search/Choose labels.
- Pinned presets: Mistral 7B Q4_0, Qwen3.5 9B Q4_0, SD-Turbo FP16, Stable Audio Open
  Small FP16, Kokoro 82M FP16. Bundles stay hidden until every file is verified.
- Downloading labels and aggregate percentage/byte progress in native and HTTP UI.
  Public GGUF browsing excludes split/mmproj files and files above 7 GiB; this leaves
  headroom but cannot guarantee every context/model configuration fits.
- HTTP Workspace category buttons choose a model of that type or open its preset category.
  History has confirmed deletion and Back to Workspace preserves conversations.

## Verification and remaining limits

- Default `make app` and native hybrid folder build/sign succeeded. Hybrid NEEDED list
  has no additional PRX dependency versus the prior working Vulkan build; entry 0x120,
  14 program headers. Native hybrid deployed only to PPSA99023; PPSA99004 untouched.
- `HOST_CXX=clang++-18 make test`: 27 tests passed. Browser interaction checks passed
  for presets, category selection, download progress, history delete/cancel/back and Speak.
- Shared model mount payload must be loaded after each console boot; see [BUILDING.md](BUILDING.md).
- Further Image/Audio console testing is outside the user's latest verification request.

## Earlier measurements

- Mistral Q4_0: Vulkan median 69.72 decode tokens/s, official upstream AGC 47.96,
  context 4096. [Benchmark protocol and limits](VK_BENCHMARK_2026-10-08.md).
- Raw folder eager load: Vulkan median 2.843 s, upstream AGC 1.526 s. mmap was reverted
  after allocation stalls; compressed ffpfsc reads remain slow. These results predate
  this UI/media integration, which has not been rebenchmarked.
  [Loading measurements](VK_LOAD_BENCHMARK_2026-10-09.md).
