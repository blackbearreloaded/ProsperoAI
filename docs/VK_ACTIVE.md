# Active work

Updated: 2026-10-08. Plan: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).

## Verified on PS5 FW 12.70

- Full ProsperoAI Vulkan UI/HTTP app runs from folder `/data/homebrew/PPSA99023`; pid 192 confirmed after final benchmark. Original PPSA99004 untouched. Temporary auto_chat.txt removed after testing.
- RADV NAVI21 device discovery succeeds, Vulkan 1.4.354. Pinned llama.cpp cb7934c52ca8710994b2ecc19775ebefcfdb8d01 cross-build works; Mistral offloads all 33 layers.
- Exact recipe-pinned Mistral 7B Instruct v0.3 Q4_0: original official upstream AGC release 01.000.000 median 47.96 tok/s; Vulkan median 69.72 tok/s (+45.4%). Three fresh launches each, same system/user text, greedy, 128 generated tokens, context capacity 4096. AGC template has 62 input tokens, llama.cpp 61. Rate counts 127 decode steps after prefill.
- Important current limitation: Vulkan reported load time ~59.9 s; AGC folder model load 1.43–1.93 s. Do not describe Vulkan as faster for cold startup.
- RADV reports 11712 MiB total heap, 11573.98 MiB free before loading. Mistral main Vulkan buffers at context 4096: 3850.02 MiB weights + 512 MiB KV + 52.01 MiB compute (~4.31 GiB), excluding driver/UI overhead. Reported capacity is not a maximum-allocation stress test.
- Build defaults: four jobs and project-local ccache. No-change llama library build 1.593 s; Vulkan folder compile/link/sign 3.776 s. `make app-vulkan-folder` skips compression.

## Resolved issues and corrections

- Folder launch refused 0x80aa001a (errno 13) until FTP SITE CHMOD 755 on eboot.bin and PRX modules. `ps5ctl.py ftp put-dir` now applies these permissions. After registration, stop the title and replace only changed files; immutable models stay installed.
- Full-app Vulkan background loading overflowed the default thread stack. Setting warmup stack to 8 MiB fixed it.
- Earlier blanket pre-main RADV-crash interpretation was wrong: captured older tests called shell exit, then the system failed executing the path "exit". The warmup stack SIGSEGV was a separate real crash.
- Failed early WIP AGC HTTP requests are not a benchmark baseline. Official upstream binary was subsequently verified to run and generate; its inference code was not modified. Temporary AGC backend diagnostics reverted.
- Shadow delete is asynchronous, may fail EBUSY during sandbox teardown, and deletes source files too. Wait for completion and source absence before uploading a replacement image. No reboot authorized or used.

## OpenAI API work

- Added Vulkan `/v1/models` and `/v1/chat/completions`, SSE, optional bearer key, usage and portable tool-call grammar. Console HTTP/auth/SSE, required lookup tool/result roundtrip and OpenCode 1.1.36 streaming connection verified. Mistral automatic read-tool choice in OpenCode hallucinated file contents; do not claim reliable autonomous coding.
- Console test config uses 16384 context via context_size.txt; the benchmark above remains at 4096.
- Image/audio/speech remain on the existing AGC build; Vulkan media migration is planned.

## Models downloader (in progress)

- Vulkan Models screen can browse public Hugging Face repos, list the eight preferred GGUF
  quantizations, download to `/data/homebrew/prosperoai/models`, and verify file size and SHA-256
  before making the file visible to the runtime. Downloads are capped at 7 GiB for memory headroom.
- Host folder build compiled after adding the downloader. The changed build has not yet been
  deployed or exercised on PS5. HTTPS redirects, TLS access under the title's privilege level,
  storage permissions and long-transfer behavior remain to be verified on console.
- Next: complete host builds/integration checks, deploy only changed files to PPSA99023 if the
  console is reachable, then test browse/download/runtime discovery without touching PPSA99004.

## Compatibility with PR #6 (midnight-ui)

This branch was rebased clean onto upstream `main` (`git merge-tree` reported no conflicts); it
does not merge against `wip/midnight-ui` itself, since that PR is still open and its native UI is
still changing. For whoever ports the Models downloader once midnight-ui lands:

- `src/model_downloader_ps5.cpp`/`.hpp` (namespace `prospero_model_download`: `State`,
  `Candidate`, `poll()`, `browse()`, `download()`) has no RmlUi dependency — only
  `gpt_app.cpp`'s `RefreshModels()` and its controller-input handling talk to RmlUi elements by
  id. The backend can be reused as-is.
- midnight-ui already names its model-library tab "Models" (`kTabs[]` in `src/native_ui.cpp`),
  matching this branch's `View::Models` — no rename needed.
- midnight-ui's Models screen (`draw_models`/`draw_model` in `src/native_ui_screens.cpp`,
  `handle_models` in `src/native_ui.cpp:395`) has no download code or networking (no
  `sceHttp`/`sceSsl` anywhere in that branch) and an explicit empty-state string ("Nothing is
  downloaded by the app"). The natural hook is a new `DialogAction` (alongside `kUseModel`,
  `kDeleteConversation`, `kCloseApp`) plus the existing `chip()`/`spinner()` pending-state pattern
  (already used for model preparation) and `ui::ToastStack` for completion/failure notices —
  follow that pattern instead of introducing new UI primitives.

## Cold-load time lead (untested)

`src/gpt_runtime_vulkan.cpp` sets `mp.load_mode = LLAMA_LOAD_MODE_NONE`, which disables mmap and
forces a full eager read+copy of the ~3.85 GiB of weights before any GPU upload. AGC loads the
same-size model from the same storage in ~1.5 s, which points at this eager-read path — not RADV
upload — as the likely cause of the ~60 s Vulkan cold load. Switching to `LLAMA_LOAD_MODE_MMAP`
is a one-line experiment worth measuring against the existing benchmark recipe before concluding
anything more structural (e.g. pipelined per-tensor read/upload) is needed.

## Evidence

- [Benchmark report](VK_BENCHMARK_2026-10-08.md)
- build/benchmark-comparison-mistral.json and individual result JSON files.
- /tmp/prospero-upstream-agc{2,3,4}-klog.txt and /tmp/prospero-upstream-agc-result.jpg.
- /tmp/prospero-vulkan-fair{1b,2b,3}-klog.txt and /tmp/prospero-vulkan-final.jpg.
- build/prospero-vulkan/PPSA99023/: current Vulkan app folder; build/agc-upstream/PPSA99004/: verified upstream release.
