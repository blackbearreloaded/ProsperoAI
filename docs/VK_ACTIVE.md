# Active work

Updated: 2026-10-09. Plan: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).

## Status after the midnight-ui merge (current)

Upstream `main` merged PR #6 ("midnight-ui"): RmlUi is gone, replaced by a native
SDL/EGL/OpenGL UI (`src/native_app.cpp`, `src/native_ui*.cpp`). This branch has been merged
onto that `main` (not just rebased — real content and modify/delete conflicts, not a stale
diff): `src/gpt_app.cpp`/`include/gpt_app.hpp`/`assets/ui/main.rml`/`assets/ui/styles/app.rcss`
are gone, taking main's deletion. `include/gpt_runtime.hpp` carries both sides' additions
(main's `gpt_runtime_prepare`/`gpt_runtime_context_full`, already called from
`src/native_app.cpp`, plus this branch's `temperature`/`model_id`/`grammar`/
`output_limit_reached`/`gpt_runtime_refresh_models`). The Vulkan-only files
(`gpt_runtime_vulkan.cpp`, `model_downloader_ps5.*`, `http_server.*`, `openai_api.inc`) moved
from `src/` to a new top-level `vulkan/` directory, because `tools/build.sh` now globs every
`.c`/`.cc`/`.cpp` under `src/` unconditionally (no exclusion mechanism) — leaving them in `src/`
would make the default AGC build try to compile `gpt_runtime_vulkan.cpp` alongside
`gpt_runtime.cpp`, a duplicate-symbol link failure. `http_server.cpp`'s only RmlUi dependency
(`ProsperoAiApp::SetExternalStatus`, a cross-thread status bridge) was replaced with a small
self-contained mutex-guarded status holder in the same file — not yet wired into the new UI's
display, since that was debug-only diagnostics, not user-facing behavior.

**Verified: `make app` builds, signs and zips the AGC/native-UI app end to end** after the
merge — the default path is intact. `make test` passes all 17 host tests (2 of
`test_native_controller.py`'s cases fail with the system default `clang++`, which resolves to
clang-14 here and is missing its ASan runtime libs; `HOST_CXX=clang++-18` passes all three —
a pre-existing host toolchain gap, not a regression from this merge).

`tools/build.sh` gained three additive, default-empty hooks for the Vulkan variant to use:
`APP_EXTRA_SOURCES`/`APP_EXCLUDE_SOURCES` (add/remove sources from the auto-discovered list)
and `APP_EXTRA_LINK_FLAGS` (raw linker arguments). Confirmed these are no-ops for the default
build (`make app` output unchanged).

**Blocked: linking the Vulkan backend into the new binary.** The RADV archive
(`libvulkan_radeon.ps5.a`, from Mihawk's reference project) was never linked as an addition to
an ordinary PS5 binary — `tools/radv-link.sh`'s `radv_link_recipe` links an entire *separate*
runtime foundation with it: Mihawk's own `libps5platform.a` (a parallel libc with `ps5_`-prefixed
replacements for dozens of functions), his own linker script (`ps5-pie-unwind.ld`), and a long
`--defsym`/`--wrap` list redirecting standard libc calls into that platform layer. The old
RmlUi-era Vulkan build worked because RmlUi rendered through SDL's *software* renderer — RADV
was the only thing touching the GPU, so the whole binary could link through Mihawk's foundation
exclusively. The new native UI renders through real OpenGL (`tools/build.sh`'s own
`ps5-pie.ld`/`app-symbols.map`/our `runtime_support.cpp` shims — a different, standard
ps5-payload-sdk foundation). Putting OpenGL rendering and RADV compute in one binary means
reconciling two runtime foundations that each assume they own the whole libc/platform layer —
real, uncertain linking work (likely a custom combined linker script and a smaller, deliberate
defsym list, not a blind merge of both recipes), not a file-list fix. This is the same class of
problem as the RADV crash investigation below, and matches midnight-ui's own PR description
flagging "graphics and inference sharing the GPU" as unverified. **Next**: scope this as its own
task — a minimal probe binary (OpenGL triangle + RADV device init in one process) before
retrying the full app, mirroring the "check the minimal case before the feature case" rule
below.

## Verified on PS5 FW 12.70 (pre-midnight-ui; the binary described below no longer exists in
this form, but the RADV/Vulkan findings still hold)

- Full ProsperoAI Vulkan UI/HTTP app runs from folder `/data/homebrew/PPSA99023`; pid 192 confirmed after final benchmark. Original PPSA99004 untouched. Temporary auto_chat.txt removed after testing.
- RADV NAVI21 device discovery succeeds, Vulkan 1.4.354. Pinned llama.cpp cb7934c52ca8710994b2ecc19775ebefcfdb8d01 cross-build works; Mistral offloads all 33 layers.
- Exact recipe-pinned Mistral 7B Instruct v0.3 Q4_0: original official upstream AGC release 01.000.000 median 47.96 tok/s; Vulkan median 69.72 tok/s (+45.4%). Three fresh launches each, same system/user text, greedy, 128 generated tokens, context capacity 4096. AGC template has 62 input tokens, llama.cpp 61. Rate counts 127 decode steps after prefill.
- Historical reported Vulkan load time ~59.9 s is superseded by the isolated loading measurements below; llama perf load time includes waiting until first evaluation. AGC still loads faster.
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

## Loading optimization verified (2026-10-09)

- PS5-only four parallel 2 MiB positional reads and four 8 MiB upload buffers;
  mmap stays disabled. Reproducible dependency hooks, host byte/cursor/fallback
  tests and PS5 library builds pass.
- Default backend logging now forwards WARN/ERROR only: synchronous debug output
  had added ~8.09 s to context creation; filtered creation takes ~29 ms.
  `/app0/vulkan_verbose_logging.txt` opts back into full logging.
- Isolated Vulkan benchmark on the raw folder Mistral GGUF, context 4096:
  first loads in three fresh processes 2.922 / 2.843 / 2.836 s (median 2.843 s).
  All greedy-token checks passed (token 29493). Fresh processes do not prove an
  uncached SSD. Prior serial+verbose weights/context median was 57.036 s.
- Official unmodified AGC release rechecked in the same test slot: model-load
  1.614 / 1.526 / 1.488 s (median 1.526 s), successful generation each time.
  AGC uses prepared model files; Vulkan uses GGUF. These are model preparation
  measurements, not total UI launch or identical cross-backend prompt tests.
- Compressed ffpfsc model reads remained ~47 s even with parallel reads;
  the verified speedup applies to an uncompressed folder GGUF.
- Legacy RmlUi app rebuilt from parent 8e1daee with the corrected runtime and
  current llama archives; NEEDED list, entry 0x120 and 14 program headers match
  the working app. Native OpenGL/Vulkan linking remains a separate blocker.
- User-authorized optimized legacy app deployed to PPSA99023 and remains running.
  Full UI warmup: device 4.16 ms, weights 2831.79 ms, context 72.87 ms,
  total 2908.83 ms (installed configuration retains context 16384). Generated
  25 tokens successfully. Original assets restored; temporary auto_chat and load
  benchmark configs removed. Exact original binary/asset backup retained locally.
- See [loading report](VK_LOAD_BENCHMARK_2026-10-09.md).

## Cold-load time: mmap tried and reverted

`src/gpt_runtime_vulkan.cpp` sets `mp.load_mode = LLAMA_LOAD_MODE_NONE`, which disables mmap and
forces a full eager read+copy of the ~3.85 GiB of weights before any GPU upload. AGC loads the
same-size model from the same storage in ~1.5 s, which pointed at this eager-read path — not RADV
upload — as a plausible cause of the ~60 s Vulkan cold load.

Tried on console (test title `PPSA99023`): switching to `LLAMA_LOAD_MODE_MMAP` confirmed the mode
switch took effect (`load_mode = mmap` in the log), but tensor loading stalled partway through
layer 31 of 33 and stayed stalled — no progress across repeated klog checks spanning minutes —
while the kernel logged repeated `FMEM allocation timeout`/`LOW FMEM` warnings. This reads as
mmap causing memory pressure on this console's RADV/FMEM path, not a speedup. Reverted back to
`LLAMA_LOAD_MODE_NONE`. The subsequent parallel-read and logging fixes reduced eager preparation to ~2.84 s
without mmap; see the loading report.

## Evidence

- [Benchmark report](VK_BENCHMARK_2026-10-08.md)
- build/benchmark-comparison-mistral.json and individual result JSON files.
- /tmp/prospero-upstream-agc{2,3,4}-klog.txt and /tmp/prospero-upstream-agc-result.jpg.
- /tmp/prospero-vulkan-fair{1b,2b,3}-klog.txt and /tmp/prospero-vulkan-final.jpg.
- build/prospero-vulkan/PPSA99023/: current Vulkan app folder; build/agc-upstream/PPSA99004/: verified upstream release.
