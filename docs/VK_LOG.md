# Run log

Append-only. Add dated entries at the end; never rewrite an existing one. Summaries for status
questions belong in [VK_ACTIVE.md](VK_ACTIVE.md), not here.

## 2026-10-05/06

- Built and installed ProsperoAI (`PPSA99004`) after moving `.deps` siblings in-repo; confirmed
  `make app` + `make ffpfsc` still produce a working build (HTTP server responds on console).
- RADV-as-payload (no title): `vkCreateInstance` returns `-3`
  (`VK_ERROR_INITIALIZATION_FAILED`) because AGC is a local stub in payload context — expected,
  since AGC is a system module unavailable outside a title.
- First RADV titles (`PPSA99014`..`PPSA99018`, our link recipe: `ps5-pie.ld`, hand-built stubs for
  `__eh_frame_*`, `__dlopen` family, `__real_fclose`/`fflush`, two AGC driver symbols): each
  registers and launches, each terminates before any of our code runs (no marker, no klog). Each
  attempt used a fresh title ID, which left stale registration records — cleaned up afterward via
  UI delete and the ShadowMountPlus API (`games/delete`, confirm=true).
- Added `tools/ps5ctl.py` commands: `agent` (control payload, port 9111, built from
  `.deps/mihawk-vulkan-review/payload/ps5vkctl`), `cat` (read a console file over FTP), `shadow`
  (ShadowMountPlus HTTP API, port 10101 — `config.ini`'s `api_bind_address` changed from
  `127.0.0.1` to `0.0.0.0` so the host can reach it; **this has no auth, LAN-exposed**).
- klog (port 3232) needed its server payload resident; restarting it
  (`/data/payloads/klogsrv-ps5.elf`) made the stream work, but no `[PS5VK]` line ever appeared for
  any RADV attempt.

## 2026-10-08

- Repo moved from `~/Projects/ProsperoAI` to `~/Projects/console/ProsperoAI` (same remote, same
  history; `.deps` and scratch carried over).
- Built Mihawk's own `radv_smoke` title fresh from his `tools/build-radv-title.sh` against our
  RADV archive (`.deps/mihawk-vulkan-review/.deps/native/radv-release`). Static validation
  (`make inspect`) reports `StaticErrors: 0`, structurally identical in shape to ProsperoAI's
  working eboot (same OS/ABI, entry point `0x120`, 14 program headers, 4 mapped LOADs). On
  console: registers (as `.ffpfsc`; folder deploy did not register — see VK_PLAN.md), launches,
  terminates within ~1s, no `[PS5VK]` klog line. Swapping his `libc.prx` for ProsperoAI's made no
  difference. **This moves the leading hypothesis from "our link is wrong" to "something in this
  console's environment differs from Mihawk's validated setup."**
- Tried ShadowMountPlus folder-registration workarounds suggested from general guidance:
  `backport_fakelib=1` + `scan_depth=2` in `config.ini`, `chmod 777` recursively on the title
  folder and then on all of `/data/homebrew`, naming per Mihawk's own folder convention. None
  changed the `0x80aa001a` ("not registered") result for a folder deploy.
- Introduced this plan/active/log structure (`docs/VK_PLAN.md`, `docs/VK_ACTIVE.md`,
  `docs/VK_LOG.md`), modeled on `.deps/mihawk-vulkan-review/AGENTS.md`'s read order and volatility
  contract, so a session can resume without re-deriving the above.

## 2026-10-08 (after console wake)

- Console had gone to standby since the earlier session today; woken via P5 Manager
  (`/api/remoteplay/wake`). Jailbroken payload loader (9021) and FTP (2120) survived standby; the
  control payload (`ps5vkctl`, 9111) and klog server (3232) did not and were resent.

## 2026-10-08 (Models downloader)

- Added a Vulkan-only Models screen and a PS5 HTTPS downloader for public Hugging Face GGUF
  repositories. It sorts common quantizations, downloads to
  `/data/homebrew/prosperoai/models`, streams to a `.part` file, verifies expected size and
  SHA-256, then refreshes runtime model discovery. Catalog listing is limited to the eight
  preferred entries; downloads are capped at 7 GiB to retain GPU/KV-cache headroom.
- Initial `make app-vulkan-folder` compiled and linked successfully. PS5 network, TLS, write
  permissions, redirect handling and a real model transfer are not yet verified.
- `/data/homebrew` content had changed entirely since the 10-05/06 session: our test packages
  (`PPSA99004`, `PPSA99014`..`PPSA99021`) are gone; new content includes `PPSA99008` (Lapy JB
  Daemon), `PPSA99169` (PS5 RetroArch, with a populated `radv-shader-cache/`), and two
  unidentified folders (`PPSA99203`, `PPSA99109`). Re-uploaded and re-registered `PPSA99019`
  (RADV Build) from the local `.ffpfsc` built on 10-06; it registered and launched the same as
  before.
- With klog capturing cleanly this time, caught the system's own crash-handling sequence for the
  first time: `[CRS][coredump_seq]` kill sequence, `SceLncService BlockingKill()`, and (on another
  attempt, during re-registration) `CrashReportSequencerReporter` events with
  `crashErrorCode=SCE_SHELL_UTIL_ERROR_APPLICATION_CRASH` and a coredump path under
  `/devlog/system/sce_coredumps.0/`. This confirms a real crash (signal/trap) rather than a
  launch-permission refusal. Two FTP races to grab the coredump directory before its cleaner
  deletes it (roughly 1s window) both missed; the path itself is reachable and otherwise empty.
- Found `PPSA99169` (PS5 RetroArch) has a non-empty `radv-shader-cache/`, meaning a RADV-linked
  title has actually rendered something on this exact console — RADV support itself is not the
  blocker; something specific to the minimal smoke title's startup is.

## 2026-10-08 (later still)

- Found Mihawk's `PS5_VulkanTemplate` on GitHub (`gh api users/mihawk-99/repos` — a full repo
  listing, not a guess), a complete console-proven RADV title foundation, separate from the
  `mihawk-vulkan-review` probe repo already checked out. Fetched just `AGENTS.md`,
  `ps5/src/platform.c` and `platform.h` via the GitHub contents API (a full `git clone` was tried
  first and killed/timed out twice — the repo is large; not needed for this).
- Its documented rule: a title must never return from `main`/`_start` (the CRT's
  `catchReturnFromMain` calls `sceSystemServiceLoadExec("exit", NULL)` then spins forever instead;
  returning or calling `exit()` both make the console report a crash over a run that worked).
  Patched `vk_std.c` to do the same, confirmed `libSceSystemService.prx` in NEEDED after relink,
  repackaged as `<TEST-TITLE-ID>`. Result: identical `ProcessTerm()` crash, with not even the first
  `radv_marker("main: entered")` line appearing anywhere. Rules out return-from-main as our
  specific cause — we never get far enough into `main` for it to apply.
- `/data/.kstuff_noautomount` confirms this console runs **KStuff**; Mihawk's `AGENTS.md` names
  **etaHEN** as the required enabler. This is now the leading hypothesis for the environment
  mismatch, unproven. Flagged for the user rather than acted on, since changing the console's
  jailbreak framework is a console-level decision, not a build-side one.

## 2026-10-08 (correction)

- The KStuff-vs-etaHEN claim in the previous entry is wrong and retracted: etaHEN itself runs on
  KStuff as its low-level backend on most current firmware (they are not alternatives), per a web
  search the user prompted by pointing out the error. `/data/.kstuff_noautomount` existing says
  nothing about whether etaHEN is present.
- Checked the more specific lead instead: `platform.c`'s Lapy daemon (`ps5_elevation_request`) for
  `/data` access. Launched `PPSA99008` (Lapy JB Daemon) and confirmed it stays resident
  (`agent procs`: `app=24 title=PPSA99008 count=1 pids=128`), then launched the RADV test
  (`<TEST-TITLE-ID>`) while it was running. Identical crash. The launch necessarily killed Lapy first
  (one foreground app at a time), so this doesn't fully rule out Lapy mattering if it could run
  truly in the background, only that launching it immediately before doesn't help.

## 2026-10-08 — RADV and llama.cpp run on console; full app warmup fixed

Reference RADV linking, platform heap wrappers, SDK libc++18 and emulated TLS produced a working <TEST-TITLE-ID> smoke. Device discovery: RADV NAVI21, Vulkan 1.4.354. Old shell-exit logs prove earlier blanket pre-main crash interpretation incorrect: tests called sceSystemServiceLoadExec("exit", NULL), then the system failed executing "exit". A bounded console coredump watcher copied no dumps for that run.

Cross-built pinned llama.cpp CPU and Vulkan static libraries. Explicit Vulkan0 selection is required because this PS5 device is classified as integrated. stories260K.gguf offloaded 6/6 layers; 32 greedy tokens matched pure CPU exactly. Inclusive timing: Vulkan 0.325 s (98.43 tok/s), CPU 0.055 s (580.89 tok/s). Tiny-model launch/dispatch overhead dominates; these are validation figures, not AGC benchmark numbers. Evidence: /tmp/prospero-llama-console2-klog.txt.

Full app UI/HTTP then discovered the GGUF after stat-based scanning. The warmup thread crashed with SIGSEGV at ggml_vk_get_device because it used the default stack; assigning 8 MiB like the generation worker fixed it. Full warmup_complete=1 and HTTP text generation verified. Evidence: /tmp/prospero-vulkan-app2-klog.txt and app3-klog.txt. Driver-reported free Vulkan memory was 11573 MiB; allocation ceiling not tested.

Builds use four jobs and project-local ccache. No-change llama PS5 build measured 1.593 s. Mistral source SHA-256 verified against checked-in recipe (19c3cf872246d48e7a9784d6def7a694a6c9056cfbeb98f7061d43cd0a7c8b7f). AGC conversion and Vulkan packaging started. HTTP generation token-limit/timing support and benchmark strict token accounting added; large-model comparison remains pending.

### Mistral Vulkan measurement, same day

Full app Mistral load succeeded, 33/33 layers on Vulkan0. Reported memory: total 12280922112 bytes (11712 MiB), free 12136202240 bytes before load. Model GPU buffer 3850.02 MiB; KV cache 256 MiB at n_ctx=2048; compute buffer 51.01 MiB. Main buffers total about 4.06 GiB, excluding driver/UI overhead. Screenshot: /tmp/prospero-mistral-loading.jpg.

Three identical API prompts, greedy sampling, 128 generated tokens each: decode 70.36, 71.39, 71.55 tok/s, median 71.39; prompt token count 29. Wall throughput 59.77, 66.78, 66.91 tok/s. Result: build/benchmark-vulkan-mistral.json. A separate 32-token request confirmed coherent output and exact token limit. AGC comparison pending.

Shadow delete is asynchronous and can fail with EBUSY (status=16) if called immediately after agent kill, before sandbox/image teardown. Wait for runtime release, retry deletion, then confirm both shadow info 404 and source file absence before uploading. The delete API actually removes the image source as well; do not upload while it is still running.

### AGC API correction for comparison

AGC app booted and discovered Mistral, but /api/generate failed before inference because gpt_runtime_generate rejected message_count < 2 while the API supplies one user message. Changed guard to require at least one message; adaptation already accepts one user message. Rebuilt AGC app and reverified package; new upload pending. Host tooling integration tests: 2 passed. Benchmark timing arithmetic tested using a mocked server response: 128 tokens / 2 seconds = 64 tok/s.

### Upstream AGC and folder deployment verified

User pointed out that RetroArch runs from a folder and that AGC should be the original upstream build. Confirmed RetroArch PPSA99169 source_type=folder. Downloaded official blackbearreloaded/ProsperoAI release 01.000.000 ZIP, verified published SHA-256 19c2bacebf6fb1587b32d458fb5af2fb472d9f3844085993b0f1a30669dc0c69. Copied its unmodified eboot.bin/libraries/assets to test slot <TEST-TITLE-ID>, only changing param.json title/concept/content ID. Added recipe-verified AGC Mistral model and built-in auto_chat.txt prompt; no inference-code changes or HTTP additions to this baseline.

Folder launch initially refused 0x80aa001a with errno=13. FTP SITE CHMOD 755 on eboot.bin and libc.prx fixed it; upstream title boots and generates coherent text. Three fresh-launch runs with default system prompt, 62 input tokens, 128 generated tokens and 4096 context capacity: decode 47.96, 47.92, 48.02 tok/s, median 47.96. Numerator is 127 decode steps, excluding the first token supplied by prefill, matching the upstream UI formula. build/benchmark-upstream-agc-mistral.json, /tmp/prospero-upstream-agc{2,3,4}-klog.txt and /tmp/prospero-upstream-agc-result.jpg preserve evidence. Load was 1.43–1.93 seconds from folder.

Earlier WIP API requests were sent while background loading was incomplete and failed at stage 2; a later captured warmup succeeded. Those failures are not evidence that upstream AGC is broken. Do not use their failed result JSON as a performance baseline. Temporary diagnostics in the AGC inference backend were reverted; baseline uses release binary.

Added app-vulkan-folder target and FTP put-dir/size/chmod commands. Folder upload applies 755 to executable/modules. Updating the stopped folder now needs only changed files; no multi-gigabyte repackaging. Vulkan being aligned to the same system prompt, 62 input tokens and 4096 context capacity before final comparison.

### Final comparison and console state

Vulkan aligned to context capacity 4096 and same default UI system/user text as upstream. Three fresh-launch decode results: 69.73, 69.72, 69.72 tok/s, median 69.72, versus upstream AGC 47.96. Same source Q4_0 model, 128 generated tokens. Prompt encoding differs by one token (AGC 62, llama.cpp 61); decode numerator is 127 after first prefill token. Vulkan +45.4% for this short-context decode test. Vulkan prefill ~0.343 s; reported load ~59.9 s vs upstream folder load 1.43–1.93 s. Main GPU buffers at context4096: 3850.02 + 512 + 52.01 MiB (~4.31 GiB). Full report: docs/VK_BENCHMARK_2026-10-08.md; JSON: build/benchmark-comparison-mistral.json.

Final console state: Vulkan folder /data/homebrew/<TEST-TITLE-ID>, pid192 running. Temporary auto_chat.txt removed, so ordinary launches do not automatically benchmark. Original PPSA99004 untouched. No-change Vulkan folder build measured 3.776 s. Host integration tests 2 passed, shell syntax and Python compilation passed, benchmark arithmetic validated with mocked response including first-token accounting, git diff --check clean. No commit or push.

Final network verification: http://<PS5-IP>:11434/ returned HTTP 200; /api/tags exposes loaded Mistral GGUF. Final screenshot confirms llama.cpp Vulkan / RADV, model loaded, HTTP server listening.

## 2026-10-08 — PR scope cleanup

Removed the Models/Settings tab redesign, model-delete UI/API and shared deletion helper from the branch diff at the user’s request. Restored the original Conversation/Workshop console layout and pre-redesign web chat. Retained HTTP networking, generation timing and the 8 MiB warmup stack fix. The rebuilt Vulkan folder compiles, links and signs; Python tooling tests pass. Hardware performance figures above apply to the previously tested binary; the UI-restoration rebuild has not been redeployed.

## 2026-10-08 — OpenAI-compatible API and OpenCode

Added Vulkan /v1/models and /v1/chat/completions, SSE text deltas, usage/finish reasons, optional bearer key on all routes, bounded request parsing, worker joining and configurable context (512–16384; default 4096). Tools use a portable schema prompt and GBNF envelope/name constraints with OpenAI tool-call IDs and tool-result history; this is not native Jinja tool templating or full argument-schema validation.

PS5 <TEST-TITLE-ID>: /v1/models/auth, non-streaming completion, SSE usage/DONE and required lookup(query=Vulkan) followed by tool result and final answer verified. OpenCode 1.1.36 direct streaming returned API_OK. Automatic read-tool choice on Mistral 7B hallucinated file contents; do not claim reliable coding-agent behavior. API tests used context_size.txt=16384, separate from the 4096-token benchmark. Temporary test key stored only outside Git. Results: /tmp/prospero-openai-console-results.json and /tmp/prospero-opencode-test/. Existing AGC build retained for media; image/audio/speech Vulkan migration planned. User requested akandr/bc250 reference as related Linux APU background, not PS5 evidence.

Additional client check: OpenCode read roundtrip succeeded via a diagnostic proxy explicitly setting tool_choice=required on the first request. OpenCode executed read(filePath=/tmp/prospero-opencode-test/fixture.txt), received the real file result and answered PROSPERO_TOOL_OK. This does not change the automatic-choice limitation above. Diagnostic proxy stopped after testing.

## 2026-10-09 (model loading investigation)

- AGC uses queued model I/O; llama.cpp eager loading uses 1 MiB upload chunks.
  Added PS5-only parallel positional reads and an 8 MiB upload chunk (four buffers,
  32 MiB ring). mmap remains disabled. Reproducible guarded dependency hooks are
  in `tools/prepare-llama-ps5-io.py`, enabled only by the PS5 toolchain.
- Host offset/multi-window/short-read checks passed, including ASan/UBSan with
  leak detection disabled because the sandbox ptrace environment prevents LSan.
  PS5 llama archives, benchmark link/sign and package verification passed.
- Important measurement correction: llama.cpp updates `t_load_us` at the first
  evaluation, using elapsed time since startup. Earlier ~60 s reported load is
  not an isolated disk-read measurement. Added explicit phase timings.
- User approved removing PPSA99019, installing the benchmark and closing
  <TEST-TITLE-ID> (PID 95) to launch it. First shadow delete stopped with EBUSY (16);
  retry after closing <TEST-TITLE-ID> completed, source absent before upload.
- Benchmark PPSA99019 launched `0x00006018`, PID 97. Report is accessible at
  `/mnt/sandbox/PPSA99019_000/download0/prospero-load-benchmark.txt`, not a global
  FTP `/download0`. Device init 7.53 ms; model load failed because the title
  sandbox cannot access another title's `/data/homebrew/...` model path.
  This run provides no model-loading speed result. Preparing a self-contained
  Mistral package to correct the test setup. Original PPSA99004 untouched.

## 2026-10-09 — loading fixes verified and legacy app deployed

Parallel PS5 eager reads plus WARN/ERROR-only backend logging reduce raw-folder
Mistral loading substantially. Three fresh optimized process first loads:
2.92159/2.84294/2.83605 s (median2.84294 s), versus isolated serial+verbose
57.03557 s median. Full-log context 8.091 s becomes ~29 ms. All serial/parallel
greedy checks matched token29493, result0. Compressed ffpfsc stays ~47 s.

Official unmodified AGC release independently rechecked on three launches:
load_us1614361/1526112/1487587, median1.526112 s, successful generation.
Backend formats and timing boundaries differ; fresh processes do not prove
uncached SSD. See VK_LOAD_BENCHMARK_2026-10-09.md for protocol and evidence.

Legacy app rebuilt from parent8e1daee using corrected runtime/current llama
archives; ELF foundation matches working app. Explicitly authorized deployment
to <TEST-TITLE-ID> completed. Full-app model ready2908.83 ms at installed context16384;
generated25 tokens and remains running. Original assets restored and temporary
auto_chat/load config files removed. Exact original backup retained. PPSA99004
untouched, no reboot. Native OpenGL/RADV linking remains unresolved.

User requested standing task-scoped console authorization in AGENTS.md;
repeated relevant test deployments/launches/cleanup no longer ask each step.
Separate permission remains for reboot, unrelated data and main PPSA99004.

## 2026-10-09 — native UI Vulkan presentation stage

User requested gradual OpenGL-to-Vulkan UI migration, preserving upstream design.
Added independent UI_PROBE target in PPSA99019 with VK_KHR_display presentation,
graphics queue, color-attachment render pass and a diagnostic attachment clear.
No EGL, OpenGL, UI redesign or model is included. Build/sign/package and ELF
foundation checks pass (same NEEDED as working standalone, entry0x120,14 headers).

Closed <TEST-TITLE-ID> temporarily; old PPSA99019 benchmark replacement initially
returned EBUSY until active app closed. Verified registration404 and source
absence before uploading the20MiB UI probe. Automatic rescan registered it.
Launch0x0000c018, PID118: all Vulkan init/acquire/submit/present calls return0;
120 frames presented and process stayed alive. Source report saved at
build/vulkan-ui-probe/console-results.txt; klog /tmp/prospero-ui-probe-klog.txt.
No screenshot: active Remote Play manager/session unavailable. API success does
not prove pixel correctness, UI parity, text rendering or inference concurrency.
Probe then closed and optimized legacy <TEST-TITLE-ID> relaunched. No reboot or main
PPSA99004 changes. Migration stages are documented in VK_UI_MIGRATION.md.


## 2026-10-09 — Native Models presets, HTTP workspace and hybrid AGC Voice

- <TEST-TITLE-ID> only: updated artwork/splash and Models trigger navigation; Triangle Space,
  R2 Done. New pinned Text/Image/Audio/Voice presets with hidden staging, size/SHA-256
  verification and aggregate progress. User confirmed matching graphics after restarting.
- Fixed mounted-leaf storage validation and explicit HTTPS/CDN redirects. Kokoro bundle
  (34 pinned files, 165171053 bytes) downloaded and installed on console.
- Native Vulkan title now links the AGC media stack with isolated old SD ggml symbols;
  global AGC lifecycle wrapper and early scratch reservation retained. Link/sign succeeded:
  17 NEEDED entries (no added module, prior Vulkan build had 18), entry 0x120, 14 headers.
- Initial hybrid boot exposed both Mistral and Kokoro but Voice HTTP stalled in a nested
  runtime-lock path. Replaced it with short catalog locks and a separate inference lock;
  metadata can be read during generation and conflicting operations fail promptly.
- Updated hybrid binary SHA-256:
  `0fb9d58fbcf9bb0866ac82eb9426a267a0953e5bf13796639e953c7a715028d3`.
  HTTP Kokoro prompt “Hello.” succeeded and produced PCM16 mono 24000 Hz WAV:
  64844 bytes, 32400 frames, 1.35 s, peak 9847, 19502 nonzero samples.
  Subsequent Mistral request in the same process returned a valid 10-token response.
  Results: `build/prospero-hybrid-{voice,text}-result.json`,
  `build/prospero-voice-agc-test.wav`. Image/Audio generation not console-tested.
- HTTP UI now lists all preset categories and download progress, confirms conversation
  deletion, supports Back to Workspace without losing history, and makes all Workspace
  category cards actionable. Speak selects installed Voice; empty media categories open
  their Models presets. Browser interaction checks passed; screenshot
  `build/prospero-web-presets-test.png`.
- `HOST_CXX=clang++-18 make test`: 27 passed. An initial model I/O test failed because
  `/tmp` was full; moving task-created binary backups to `build/` restored space and the
  full suite passed. Default `make app` also succeeded. Main PPSA99004 untouched.
- Final web assets uploaded only after <TEST-TITLE-ID> reported `mounted:false`; title
  relaunched. Live browser test against the console HTTP server passed: Speak selects
  Kokoro, Back preserves history, current-conversation deletion returns to Workspace,
  and empty Image opens the SD-Turbo preset. Screenshot `build/prospero-web-live-test.png`.
  Final native rebuild reproduced the same binary SHA-256. Test title remains running.


## 2026-10-09 — Release preparation and automatic model storage

- PR #5 merged to main at c00dec63885b64caabaff4e1816dcba226949985. README uses
  the current PS5 golden doorway icon. Release preparation is PR #8, 01.001.000.
- Native build embeds a title-scoped helper at assets/platform/model-mount.elf. Startup
  checks the private storage marker, sends the helper only to the console-local ELF loader
  on 9021 if needed, handles partial writes and waits for the mount. Networking startup
  is shared with HTTP; discovery rescans after setup.
- <TEST-TITLE-ID> launched the helper itself: new resident PID92, successful mount, raw Mistral
  and Kokoro visible in HTTP/native catalogs. No new helper was sent manually. Evidence:
  build/prospero-auto-mount-log.txt and build/prospero-auto-mount.jpg.
- Host suite: 28 passed, including partial-send, loopback-only destination, invalid-ELF,
  connection/send failure cleanup and storage-readiness tests. Lint/native build succeeded.
- Release-preparation CI at 9782055 succeeded in 14m17s, building RADV from pinned
  sources and the hybrid app. Production PPSA99004 has not been deployed on the console.


## 2026-10-10 — PR #15 host-only context / automatic KV continuation

Implemented native/web context and Thinking settings, persisted authenticated runtime
settings API, Auto budgeting and context-only recreation, transformer prefix reuse and
file-size fit badges. User clarified that only context is manually selectable; KV precision
remains automatic. Hybrid/recurrent prefix reuse remains disabled. Download fit is a rough
catalog-size estimate, not a measured allocation guarantee.

Host evidence: 35 tests passed with clang++-18 (ASan/UBSan controller included); HTTP covers
persistent settings, bad-value rejection and auth. Native Generation preview rendered and
inspected; JS syntax passed. `make app-vulkan-folder` built/signed <TEST-TITLE-ID>, entry 0x120,
14 program headers. Compared with build/prospero-vulkan/eboot.elf, no new PRX dependency
was added; libSceHttp2, libSceSsl and libkernel_sys are absent from the new NEEDED list.
Changes are local and unpushed. No console access/deployment, launch code, klog or hardware
screenshot exists for this continuation; all hardware gates remain pending. Completed and
unfinished items are listed in PLAN_CONTEXT_MODELS_VULKAN.md and VK_ACTIVE.md.

## 2026-10-10 — host media migration attempt and final runtime goal

User clarified that the final target is one shared Vulkan/ggml runtime if faster, with dual
runtime only during comparison. Updated static gates and continuation plan accordingly.
Lapy remains pinned to release 01.003.000 source/SDK/protocol; test helper SHA-256 matches
previous test build: `7336b7c9ac15d7b6a6bd02eec1b953355c87d8b7019e0f146846a9f2643ff69a`.

Experimental audio/TTS bridge uses shared Vulkan for matrix products, attention, linear and
selected unary kernels. Remaining CPU kernels and legacy SD remain. Host real-Vulkan parity
passed 22 graphs on llvmpipe; full suite passed 36 tests (`build/media-migration-full-tests.log`).
`make app-vulkan-media-folder` built/signed (`build/media-vulkan-build-final.log`), entry
0x120, 14 program headers and unchanged baseline NEEDED dependencies. No PS5 speed evidence.

Pinned SD source `f89d9b13d730eabeede7314ce49dacd18d3c90c2` compile probe fails honestly:
`GGML_MAX_NAME >= 160` versus shared ggml 64 (`build/sd-vulkan-probe-final.log`). Tensor
layout changes, so an SD-only macro override is unsafe; shared producers/consumers must be
rebuilt with a common ABI. Pocket TTS archive/source migration remains unproved.

No console connection/deployment, registration change, commit or push in this continuation.

The default `make app-vulkan-folder` also rebuilt/signed successfully after the media
changes (`build/media-baseline-regression.log`), confirming the feature-off build still links.

## 2026-10-10 — shared SD/llama ABI and additional audio convolution work

Host-only continuation resolves the earlier SD 64-vs-160 tensor ABI blocker with a new
isolated CMake stack: llama, ggml-base/CPU/Vulkan, SD and consumers all use 160. The shared
native folder uses source SD on Vulkan0 and omits private SD/tts ggml archives. Its AGC
adapter compiles direct-only for legacy text, not as a ggml backend. Default packaging
still retains the comparison paths until console gates pass.

`BUILD_JOBS=2 make app-vulkan-shared-folder` built and signed <TEST-TITLE-ID>
(`build/shared-vulkan-app-build-final.log`). ELF entry 0x120, 14 program headers, identical
baseline NEEDED dependencies, one defined ggml_init, no private ggml/PS5AGC backend symbols
(`build/shared-vulkan-elf-validation.log`). Default folder rebuilt and signed after changes
(`build/shared-vulkan-baseline-regression.log`). Lapy helper remains byte-identical to the
previous test helper (SHA-256 recorded above).

The PS5 loader initially rejected optional unresolved TLS init hooks for SD's executor
pointer/active flag. C++20 constinit declarations suppress those hooks for upstream's
nullptr/false initialization while retaining upstream per-thread storage. Host libstdc++
also needed recursive PickleValue special members defined after class completion; a
generated host-only patch keeps the pinned checkout immutable. SD reports its exact source
commit rather than the parent app's Git commit. See tools/shared-vulkan and VK_MEDIA_MIGRATION.

Audio/TTS conv_1d and conv_1d_t4x4 now use Vulkan matrix products with bounded CPU im2col
staging (256 positions), preserving stride/padding/dilation/bias and channel-first output.
Numerical parity covers two tiles and both bias states: 30 real Vulkan graphs pass.
Shared host checks also passed long tensor names, SD device discovery, thread-local
executor behavior, torch state-dict metadata/truncation parsing and actual tiny GGUF
Vulkan decode with finite logits (`build/shared-vulkan-host-validation.log`). Vulkan uses
llvmpipe; no PS5 GPU speed claim or full SD image/end-to-end waveform validation.

`TMPDIR="$PWD/build/context-validation-tmp" HOST_CXX=clang++-18 make test`: 37 tests passed
in 92.761 s (`build/shared-vulkan-full-tests.log`). Host shared build verified separately
(`build/shared-vulkan-host-build-verified.log`). No console connection/deployment, title
registration, release-version change, commit or push. PR #15 summary updated separately.


## 2026-10-10 — Six follow-up improvements added to the plan

Added six explicitly pending tasks: media buffer/graph optimization, Auto context
allocation recovery, persistent outputs with metadata, generation limits/status,
download resilience and mandatory shared Vulkan CI. Host priority is outputs,
downloads and CI, then media overhead; hardware acceptance gates remain unchanged.
Updated the detailed plan and active summary, and prepared the same checklist for
PR #15. This entry records planning only: no implementation, console access, commit
or push.

## 2026-10-11 — Upstream integration and live test-title validation

Integrated merged #19 with safer prefix reuse from #17 and optional MiniCPM5 support
from #18 into the local continuation. Changes remain uncommitted/unpushed. PR #15 was
updated and marked ready at the user's request; #17 currently conflicts with main.

Final host suite: 49 tests, one opt-in skipped; actual llama prefix regression separately
passed under ASan/UBSan. Lint and both normal/shared signed folder builds passed.

On FW 12.70, <TEST-TITLE-ID> normal/shared builds booted. Mistral repeated 918-token prompt
prefill improved from 2320.505 ms to 13.118 ms with matching greedy outputs; edited
prefix, 8K/16K contexts, retained weights on context recreation and both Ollama routes
passed. A max_tokens=4096 request ended naturally at 1206 tokens. Shared text/Kokoro/text
passed; WAV was non-silent mono 24 kHz, 52200 frames. This is functional evidence,
not perceptual parity or controlled performance acceptance. MiniCPM download resumed
from approximately 421 MB after intentionally stopping/replacing the test build.
Evidence: docs/performance/2026-10-11/integration-results.json and integration-runtime.txt.
PPSA99004 was untouched; no reboot. Full SD/audio and fresh repeated AGC comparisons remain.

Researched AirLLM at pinned commit 8df7eebe974a2c7cd120b334cd211cd25624cfdc.
Kept resident GGUF/Vulkan primary and revised Phase 4c to measured native streaming
feasibility gates; removed unsupported speed assumptions. See AIRLLM_PS5_RESEARCH.md.

MiniCPM download completed: pinned SHA verified and installed, 2679710688 bytes.
Console inference returned Four with Thinking off, and a reasoning block plus Four
with Thinking on. Restored Auto context/thinking off afterwards. Evidence:
performance/2026-10-11/minicpm-console-results.json.

PR #17 reconciled with merged main/#19 in isolated WIP worktree. Commit d5e2d87
was pushed without force to the original codex/vulkan-prefill branch. Preserved both
cache safety and #19 model/context/output API behavior. Lint, 35 host tests (one opt-in
skipped), actual llama ASan/UBSan cache regression and signed native Vulkan/AGC folder
build passed. GitHub reports MERGEABLE; CI was queued at verification. Updated both
#17 and #15 descriptions. Our broader working tree remains uncommitted/unpushed.

PR #17 merged upstream as bdef791. Reconciled PR #18 against this main in isolated
WIP worktree and pushed a6b9b0b to original codex/minicpm5-q8 without force. Kept
MiniCPM explicit BOS in both tokenization passes and main cache/model/context/output
behavior. Lint, 39 host tests (one opt-in cache skipped), all four MiniCPM tests
including official Jinja oracle, separate actual llama ASan/UBSan cache regression,
and native Vulkan/AGC folder cross-build/signing/framing passed. GitHub MERGEABLE;
new CI running. Updated #18 and #15 descriptions. No console deployment for this
standalone reconciliation, and no change to the broader unpushed continuation.


## 2026-10-11 — Exact Ternary Bonsai 2 native baseline and preset

- User's top-priority model is prism-ml/Ternary-Bonsai-2-27B-gguf PTQ1_0,
  revision b072e1d3b35a0a630cece372c2127528e0994386. Host SHA256 and full 5.947 GB
  console FTP readback match 53107f530aa52eb00912263ab1ee29bd199261c87cd7b4ad4ca1318c1fe33ee3.
- Stock types do not describe Prism private types 142/143 or Hadamard graph ops.
  Build the complete pinned Prism runtime e8fd209, guarded PS5 I/O hooks, separate
  experimental app folder and runtime-gated pinned preset. Preserve existing
  GGUF/AGC bundles and media. Final eboot SHA256 is
  1abdea8e26b8446b44f145218aae4fbc0cc6dc0d7e54420d9c7e10ef895380f9;
  entry 0x120, 14 PH, same 15 NEEDED as working stock build. Use app-bonsai-folder.
- First native generation succeeded. Final build adds correct Qwen Thinking suffix
  and displayed opening marker (without extra generation/KV accounting), conservative
  Auto fallback for unsigned-wrapped Vulkan free reports, and the Vulkan audio bridge
  with Prism's older mul-mat precision API. 4K/16K and Thinking off/on pass.
- Controlled resident decode: three identical greedy 94-token outputs at 4K/f16,
  11.207590 / 11.209078 / 11.206914 tokens/s, median 11.207590. HTTP times 9.115 /
  9.119 / 9.123 s, cold one-word 5.634 s. The ~15 tokens/s target remains unmet.
- Mistral GGUF, MiniCPM5 Q8, Qwen3.5 9B Q4 and return to Bonsai pass. Qwen source
  hash and console file size verified; do not confuse this with Bonsai's full readback.
  Legacy AGC Mistral → Kokoro → Bonsai passes in the same final build. Kokoro WAV:
  non-silent mono 24 kHz, 52200 frames; 85.630 s functional request. Earlier legacy
  voice also generated a valid WAV (93.160 s); the first FTP test used an unexpanded
  /download0 alias. Actual WAV is mapped into /data/prosperoai/work/tmp.
- 54 host tests complete (one opt-in skipped), lint passes, actual Prism cache test
  passes ASan/UBSan, and Prism/llvmpipe media numerical parity passes (30 GPU calls).
  Stock build and conditional preset catalogs compile. Full host CLI build was not
  established; host static libraries are the cache/numerical-test reference.
- Native AirLLM/Strata S0 parser and 16 MiB disk probe pass. Bonsai layers 5.379 GB,
  pair of largest slots 169.822 MB, resident nonlayer 556.257 MB. Uncontrolled-cache
  disk-only median 1.384/1.778 GB/s serial/four workers. ~0.33 tokens/s I/O-only full
  layer streaming bound; no upload or streamed-inference claim. S1 remains pending.
- <TEST-TITLE-ID> remains running; saved settings restored Auto/Auto/Thinking off.
  PPSA99004 untouched, no reboot. Image/audio models not installed: those paths,
  perceptual parity, larger context, sustained performance and streaming remain gates.
  Raw evidence: performance/2026-10-11/bonsai2-*.json and streaming-bonsai2-*.json.

## 2026-10-11 — installed media and comparison launch recovery

- SD-Turbo (8 files) and Stable Audio (73 files) installed with pinned revision,
  per-file host SHA256 and complete console readback; all 81 passed. Receipt:
  performance/2026-10-11/bonsai2-media-install-verified.json.
- Prism initial smoke: SD 73.085 s, coherent 512×512 apple image; Stable Audio
  152.424 s, stereo 44.1 kHz PCM16, 524288 frames, non-silent, zero clipped samples.
  Actual outputs downloaded and checked. Text return to Bonsai answered 4.
  These are functional initial timings; SD uses AGC in this Prism build.
- After eboot replacement, launch refused 0x80940033 and UI CE-105773-3.
  klog LNC_CHECK reported m_is_pkg=false. Registered test slot was unmounted.
  Restored known-good Prism also refused. Explicit ShadowMountPlus /games/mount
  resolved it; Prism launched 0x0000e018. AGC comparison then launched 0x00000018.
  No reboot, no new title IDs, PPSA99004 untouched. Benchmark underway.
- Final goal remains one Vulkan inference runtime for all supported models.
  Remove dedicated AGC inference after media parity/performance/memory gates;
  unify exact Prism Bonsai and shared media on one ABI before claiming one runtime.

## 2026-10-11 — AGC versus shared Vulkan media measurements

| Model | AGC initial s | Vulkan initial s | AGC repeat median s | Vulkan repeat median s | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| SD-Turbo | 73.482 | 30.045 | 46.048 | 1.001 | 46.025× |
| Stable Audio | 396.044 | 152.796 | 368.103 | 124.106 | 2.966× |
| Kokoro | 88.992 | 89.223 | 84.740 | 85.152 | 0.995× |

- All three models: initial + three repeats, artifacts validated, text return after
  each group. All pass; settings restored Auto/Auto/Thinking off.
- SD correlation 0.992512, MAE 2.754/255; inspected both apple images. Audio
  correlation 0.999975; Kokoro 0.996637; correct PCM rates/frames, no clipping.
- Kokoro has no speedup (~0.5% slower); peak memory and extended switching unmeasured.
  Performance compares complete builds, with prebuilt legacy versus pinned source SD.
- Common Prism/shared media ABI and legacy text migration still required before AGC
  retirement. Raw records: performance/2026-10-11/media-{agc,vulkan,comparison}.json;
  methodology: MEDIA_VULKAN_COMPARISON.md. Restoring verified Prism test build next.

## 2026-10-11 — final console state after media comparison

- Restored verified Prism eboot in unmounted <TEST-TITLE-ID>; chmod, shadow mount and
  launch passed (0x00004018). Exact Bonsai returned 4 after restoration.
- Settings Auto/Auto/Thinking off; SD/audio/voice/Bonsai/old model installations
  remain available. Receipt: performance/2026-10-11/media-final-return-bonsai.json.
- Added ps5ctl shadow mount command and standing test-deployment authorization
  to AGENTS.md. Final lint passes; changes remain local/uncommitted/unpushed.
- PPSA99004 untouched; no reboot.

## 2026-10-11 — unified vulkan-runtime and PR scope split

User authorized AGC inference retirement, runtime naming and publishing verified code
in PR #15, with remaining planned work in a separate draft. Built complete Prism/SD
against GGML_MAX_NAME=160; removed private ggml and AGC inference/scratch/routing.
RADV platform AGC imports remain necessary. Signed test eboot 10758bc079a5895f558c6b1362a047cdca14c5e2df32d6d89712d2e11d26873f;
entry 0x120, 14 program headers, working 15 NEEDED, one ggml_init, no AGC inference
or private ggml symbols. The test title was stopped, mounted:false verified, eboot uploaded, chmod,
explicit shadow mount, launch result 0x00006018. No UI confirmation or reboot required.

Bonsai warm median 11.202328 t/s; Thinking/4K/16K and Mistral/MiniCPM/Qwen regressions
pass. SD 29.787 s, Audio 153.245 s, voice 89.337 s initial requests including load;
outputs byte-identical to prior Vulkan comparison and text switches pass. Settings
restored Auto/Auto/Thinking off. PPSA99004 untouched; models remain installed.
Common-ABI host Vulkan decode/parity, real-model ASan/UBSan prefix tests, complete host
regressions and lint pass. Release folder/ZIP builds. Sandboxed full-suite attempts
were blocked by LSAN ptrace/local socket fixtures; clean execution outside that
restriction passes, without disabling sanitizer in the final full-suite run.

Records: performance/2026-10-11/vulkan-runtime-{build,bonsai,media,legacy-alias}.json.
Voice speedup, peak memory/thermals and streaming are not claimed. These and S1
bounded residency move to the subsequent draft PR.

## 2026-10-11 — PR #15 release-candidate verification

- Repeated the full Bonsai console check through the app HTTP server. Cold correctness,
  three identical warm outputs (11.208128 tokens/s median), Thinking mode, 16K context,
  Mistral/MiniCPM5/Qwen regression and return to Bonsai passed.
- Repeated the media check through the app HTTP server. SD-Turbo completed in 29.649 s;
  Stable Audio in 147.683 s; Kokoro in 89.228 s. The image and WAV artifacts validated,
  neither WAV clipped, and text generation passed after every media model.
- Shared conversation create/read/delete passed on the console HTTP server. The production
  title was untouched and the console was not rebooted.
- Prepared `01.004.000` metadata and English release notes. Removed the private test-title
  ID from tracked documentation, tools and archived evidence; test tools now require explicit
  title variables. The remaining release gate is an old-version-to-candidate updater run on
  a test title, followed by the final candidate build, host suite and CI.

## 2026-10-11 — old-release update and final PPSA99004 verification

- Backed up the installed `PPSA99004` folder and confirmed its registered version was
  `01.003.000` and unmounted. Its eight installed model entries were already in the shared
  `/data/prosperoai/models` store, so no duplicate model transfer was needed.
- Built the `01.003.000` Vulkan source with the documented development-offer switch and
  pointed it at a temporary GitHub prerelease containing the exact candidate ZIP. The old
  app showed the `01.004.000` offer, saved a screenshot before confirmation, downloaded and
  verified the archive, closed, and let the helper replace the folder. The installed
  `param.json` matches the candidate and reports `01.004.000`.
- The updater-installed `PPSA99004` candidate launched successfully. Bonsai cold correctness,
  three identical warm outputs (11.205240 tokens/s median), Thinking, 16K context,
  Mistral/MiniCPM5 regression and return to Bonsai passed through HTTP.
- Media passed through HTTP: SD-Turbo 30.324 s, Stable Audio 148.888 s and Kokoro 89.014 s.
  Artifacts match the prior candidate hashes, WAV properties are valid with no clipping, and
  text generation passed before media and after every model.
- Final host validation: lint passed; all 50 tracked tests passed with one opt-in real-model test
  skipped; the release ZIP contains 121 entries, the ELF has entry `0x120`, 14 program
  headers and 15 NEEDED libraries, and `self-updater.elf` is present.

## 2026-10-11 — extended output and context limits

- Replaced the 4096-token output cap and 16K request-context cap with shared validated
  limits. The current API/native ceiling is 131072 output tokens and 262144 context tokens;
  response/session storage is dynamic with a 2 MiB byte guard.
- Host HTTP tests, native controller/session tests, lint and the PS5 Vulkan folder build pass.
- On `PPSA99023`, MiniCPM5 generated 32712 completion tokens from a 56-token prompt,
  filling a 32768-token context and returning `finish_reason: length`. This exceeded both
  the old 4096-token and 32 KiB response limits without a crash or truncation.
- Added `Qwen3.5-0.8B-Q4_0.gguf` (563036064 bytes, SHA-256
  `57d1997790d1744fba5b40a7317df71ea5e2acee28c47e78f0cce39c0703f8cf`) as the small
  long-context probe. It loaded at `context_tokens: 262144` and returned the requested
  six-token smoke response. The first 131072-output request ended normally after one token
  because the model selected EOS, so a full 128K output is not yet verified.
- An initial 2 MiB fixed native streaming buffer caused a pre-main/early-start failure;
  moving that buffer to heap storage restored a clean launch and HTTP service. No reboot.

## 2026-10-11 — auto output fill, VRAM-derived fit estimate, web media rendering

- An omitted `max_tokens`/`num_predict` (API) or "Auto" `output_limit` (native Response
  length, now the default) fills the remaining context instead of stopping at a fixed
  512-token default; the existing remaining-context clamp and EOS stop still bound it.
  Native UI gained an "Auto" entry on Response length, mirroring Context length's.
- On `PPSA99023`, Qwen3.5-0.8B with no `max_tokens` generated 8151 completion tokens into an
  8192-token context (`finish_reason: length`, `total_tokens: 8192`), and separately 32726
  into a 32768-token context (`total_tokens: 32768`) — the auto fill tracks whatever context
  is requested, not a fixed ceiling.
- `prospero_model_download::estimate_fit` now derives its comfortable/ceiling thresholds from
  the Vulkan backend's actually reported heap (`gpt_runtime_vulkan_heap_bytes`, forwarded
  through the text/media router) instead of a hardcoded figure, falling back to the known
  11,712 MiB RADV figure when no device is registered (host tests).
- Added a `GET /download0/<path>` HTTP route, gated by the same bearer-key check as the rest
  of the API, serving only the fresh-output and archived-media paths the runtime itself
  writes (mirrors the native app's own `media_path`/`refresh_images` allowlist); rejects any
  other path or one containing `..` with 404. Verified on `PPSA99023`: fetching a freshly
  generated SD-Turbo `.tga` returned the expected 512x512x4+18-byte file with a correct
  header, and a Stable Audio `.wav` returned a valid RIFF file; three traversal/disallowed
  paths were rejected with 404.
- The web UI previously showed the raw `/download0/...` path as text with no way to view,
  play or download the result (the native app already renders these inline). `app.js` now
  detects the same markers, strips them from the visible text, and fetches the file through
  the new route: a `.tga` is decoded client-side (plain 32-bit uncompressed TGA, no new
  dependency) onto a canvas with a PNG download link; a `.wav` gets an `<audio controls>`
  element and a download link. CSP's `img-src`/`media-src` extended to allow `blob:`.
- Regression after all of the above, on `PPSA99023`: Bonsai answered correctly (bounded
  `max_tokens`), SD-Turbo completed in 29.7 s, Stable Audio in 153.1 s, matching prior
  baselines. Host lint and all 55 tests pass; the PS5 Vulkan folder build links cleanly.

## 2026-10-11 — web UI fixes: empty-draft history, mobile history drawer, 256k settings

- `/api/settings/runtime`'s POST handler validated `context` against a 7-entry `{auto..128k}`
  table while its `values[]` already had an 8th (262144) entry from the 131072/262144 change
  above; `"256k"` was silently rejected. Added the missing option and loop bound; a host
  test now round-trips `256k` through the settings API. The web Settings page's context
  `<select>` was missing the matching `<option>` too.
- Starting a web conversation without sending a message no longer appears in history or
  syncs to the console: `newConversation()` used to unshift it into the saved list and
  persist it immediately, cluttering history with empty "New conversation" entries. The
  conversation now only joins the list (and gets synced) once its first message is sent.
- The mobile conversation drawer left history-only elements (the "+" new-conversation
  button) behind a 15%-wide sliver with no way back to the chat except re-finding the
  header's hamburger; switched it to a full-screen drawer with its own visible close
  button, and "+" now closes the drawer after creating the conversation.
- Verified server-side conversation sync directly (create-with-first-message POST, catalog
  GET, detail GET, delete) against `PPSA99023`; unaffected by the client-side timing change
  since the sync endpoint itself was not touched. No browser automation tool was available
  in this environment to click through the page itself. Host lint and all 55 tests pass.

## 2026-10-11 — mobile layout review via headless Chromium against PPSA99023

- No browser automation tool was connected in this environment, so the web UI was actually
  rendered and screenshotted at a 390x844 mobile viewport with a local headless Chromium
  driven over its own DevTools protocol directly against `PPSA99023`'s HTTP server, rather
  than inferred from CSS alone.
- Found and fixed: the Models page's search input and type filters stayed in one flex row
  on mobile (only `gap` was overridden there), collapsing the search box to an unusable
  sliver next to wrapped filter pills; now stacks in a column. The chat header's Back
  button, history toggle, title and model chip wrapped unpredictably across the same row;
  reordered with flex `order` into back+toggle, then a full-width title row, then the model
  chip, each on its own line.
- Re-screenshotted after each fix to confirm, then exercised an actual message round trip
  (title updates from the real first message, response renders, "Completed" status shows)
  and a simulated long conversation (message bubbles wrap correctly, `#messages` scrolls
  independently of the fixed header/composer, auto-scroll-to-bottom works). Welcome and
  Models card/list layouts were already sound on mobile. Host lint passes.

## 2026-10-11 — raised the HTTP server's hard connection cap

- The web client's own cold page load (index.html, app.css, app.js, 3 fonts, plus the
  immediate `/v1/models`, `/api/conversations` and `/api/models/download` calls) routinely
  opens more concurrent connections than the server's hard cap of 4, so "Conversation sync
  unavailable: Too many active connections." could appear on a normal load, not only under
  the heavy automated testing above. Raised the cap (and the matching listen backlog) from
  4 to 8; each connection's own 8 MiB thread stack is ordinary system RAM, not the
  constrained Vulkan heap, so this does not compete with model/KV budgets.
- Verified on `PPSA99023`: 8 concurrent `/v1/models` requests all returned 200 (previously
  4 would have been rejected), and a realistic cold-load burst (the 6 static assets plus
  `/v1/models` and `/api/conversations`) all succeeded concurrently. Host lint and all 55
  tests pass.

## 2026-10-12 — RDNA2 UMA mul_mat_vec row heuristic (PS5-only), measured +4%

- GPU timestamps on `PPSA99023` identified the decode hotspots on Mistral 7B Q4_0:
  `MUL_MAT_VEC q4_0 m=14336,n=1,k=4096` at ~6.0-7.7 ms and the fused
  `MUL_MAT_ADD ... m=4096,k=14336` at ~3.5-5.3 ms out of ~15.8 ms GPU time per token.
  Profiling itself costs throughput, so its tokens/s is not comparable with a normal run.
- The pinned llama.cpp gives AMD GCN `rm_stdq = 2` (4 output rows per workgroup for
  standard quants) but leaves RDNA2 at the conservative default of 1 (2 rows). The PS5-only
  build patch in `tools/prepare-llama-ps5-io.py` now sets `rm_stdq = 2` for
  `AMD_RDNA2 && uma`, which is Oberon here, and leaves the generic upstream heuristic alone.
- Fair A/B, identical folder deploy and identical protocol (Mistral 7B Q4_0, context 512,
  batch/ubatch 128, 64 generated tokens, 3 runs, no profiler build):
  reference median `62.9287` tokens/s, 4-row variant median `65.5106` tokens/s, about
  `+4.1%`. All 6 runs passed with valid deterministic output.
- The winning `eboot.bin` was rebuilt from the committed patch and deployed over FTP with
  mode `755`; the deployed build re-measured `65.4261` tokens/s median (3/3 passed), which
  confirms the gain comes from the committed source and not from the experiment tree.
- Evidence: `performance/2026-10-12/rdna2-rm-stdq-{baseline,4rows,4rows-deployed}.json`.

## 2026-10-12 — RDNA2 UMA mat-vec workgroup size, and two rejected variants

- Continued the same mat-vec tuning with identical protocol (Mistral 7B Q4_0, context 512,
  batch/ubatch 128, 64 generated tokens, 3 runs, folder deploy of only `eboot.bin` at `755`).
  Each variant was built from the pinned source, deployed and measured on `PPSA99023`.
- Rejected: eight output rows per workgroup (`rm_stdq = 4`) regressed to `61.0711` tokens/s
  from the four-row `65.4261`, so four rows is the peak of that knob, not a lower bound.
- Accepted: upstream enables the larger hybrid-reduction mat-vec workgroup
  (`DMMV_WG_SIZE_LARGE`, 4x subgroup threads) only for NVIDIA and Intel. Enabling it for
  `AMD_RDNA2 && uma` on the narrower projections (`m <= 8192 && k >= 1024`, excluding Q6_K
  as upstream does) measured `66.4924` tokens/s, about `+1.6%` over four-row alone, with all
  three runs (66.34-66.69) above the previous spread.
- Rejected: removing the `m <= 8192` limit so the 14336-row FFN mat-vec also uses the large
  workgroup regressed to `62.1261` tokens/s. The hotspot wants few, wide workgroups; only
  the narrow projections benefit from spreading.
- Both accepted patches together, rebuilt from the committed `tools/prepare-llama-ps5-io.py`
  and deployed, measure `66.6161` tokens/s median (3/3 passed) against the `62.9287`
  baseline, about `+5.9%`. A greedy correctness prompt on the deployed build answers
  correctly (primes and Tokyo), so the gain is not from degraded output.
- Host suite: 56 of 58 tests pass with `HOST_CXX=g++`. `test_downloader_fit` fails only on
  g++ `-Werror` warnings that clang does not emit, and `test_self_update` is the known
  managed-sandbox Unix-socket limitation. With the default clang, 7 tests cannot link
  because this host is missing `libclang_rt.asan`; none of this touches the build patch.
- Evidence: `performance/2026-10-12/rdna2-rm-stdq-8rows.json`,
  `rdna2-dmmv-large-wg-m8192.json`, `rdna2-dmmv-large-wg-all-m.json`,
  `rdna2-combined-deployed.json`.

## 2026-10-12 — forcing wave32 for mat-vec showed no gain; run-to-run noise quantified

- Forcing a required subgroup size of 32 for the mat-vec pipelines on `AMD_RDNA2 && uma`
  (through `subgroup_size_control`, also scaling the workgroup sizes) measured `65.9353`
  tokens/s against the committed build's `66.6161`, so it was reverted rather than kept.
- Re-measuring the unchanged committed build right afterwards gave `66.1684` tokens/s, so
  repeated identical builds vary by roughly `0.5` tokens/s under this protocol. The wave32
  result is inside that band: the honest reading is no measurable gain, not a regression.
  It also means the workgroup-size step above (`+1.06` tokens/s) is a small effect at the
  edge of the noise, while the four-row step (`+2.5`) and the combined `62.93 -> 66.6`
  (about `+5.9%`) are well clear of it.
- The console now runs the committed build from `4d67511`, re-uploaded and verified after
  the experiment.
- Evidence: `performance/2026-10-12/rdna2-dmmv-wave32-rejected.json`,
  `rdna2-combined-restored.json`.

## 2026-10-12 — Kokoro profile: the GPU is not where its time goes

- Kokoro does not use ggml at all. It is the prebuilt `vendor/lib/libkokoro-tts.a` (an Adreno
  OpenCL port of kokoro-82m) running against the hand-written OpenCL implementation in
  `inference/backends/vulkan/opencl_compat.cpp`, which already forwards mat-mul and unary kernels to Vulkan through
  `prospero_media_vk` and runs everything else on the CPU.
- Added per-kernel accounting to that bridge (`begin_kernel`/`end_kernel`/`record_host`) plus
  timers on the data-moving OpenCL entry points, and routed `print_stats()` into klog through
  a log sink, since a PS5 title drops libc's stderr.
- Measured on `PPSA99023`, one sentence, 79800 samples at 24 kHz (3.3 s of audio), 131.3 s
  wall: all OpenCL kernels together account for `1.69` s on the CPU and `0.29` s on the GPU,
  and the OpenCL API timers are too small to reach the top sixteen. The most expensive CPU
  kernels are `lstm_seq` (12 calls, 0.84 s), `weightnorm_recon` (38, 0.42 s), `pad_rows`
  (10, 0.11 s) and `im2col_ncl` (33, 0.065 s).
- So about 98.5% of the wall time is inside the prebuilt archive's own CPU code, which never
  reaches our OpenCL layer. Porting the remaining shim kernels to Vulkan can save at most
  about 1.7 s of 131 s, which also explains why the earlier AGC/Vulkan media comparison
  measured voice as roughly 0.5% slower: it optimized the wrong layer.
- Evidence: `performance/2026-10-12/kokoro-kernel-profile.txt`.

## 2026-10-12 — kokoro.cpp (ggml/Vulkan speech) builds and runs on the host

- Pinned `kokoro.cpp` at `a9e31430838c8bdc3d4beaaab62753681b9f7839` as `kokoro-cpp` in
  `tools/deps.json`, checking out only its Highway submodule: `KOKORO_GGML_DIR` points at the
  pinned Prism ggml this build already uses, so no second ggml copy enters the tree.
- `tools/build-kokoro-host.sh` configures it against `build/prism-llama-source/ggml` with
  `.deps/Vulkan-Headers`. Without the pinned headers ggml-vulkan fails to compile, because
  Debian's 1.3.239 headers have no `VK_EXT_layer_settings`. CMake reports
  "using external ggml checkout" and "Vulkan backend: enabled"; its own 7 tests pass.
- Host measurement, `kokoro-82m-f16.gguf` plus the `af_heart` voice pack, one sentence,
  78600 samples at 24 kHz (3.28 s of audio), `KOKORO_BENCH=1`:
  CPU (4 threads) `3959.81 ms` total, of which the generator is `3578.89 ms` and the decoder
  `131.13 ms`. The same run on this host's Intel HD 630 Vulkan takes `8335.10 ms`, which says
  nothing about RDNA2 and only proves the Vulkan path works end to end.
  For contrast the console's current prebuilt OpenCL Kokoro needs about 130 s for the same
  sentence, so even a weak host CPU on ggml is roughly 33x faster than what ships today.
- CPU and Vulkan output: identical length and RMS (1061.6 vs 1062.6), frame-envelope
  correlation `0.9983`, while raw samples differ (correlation 0.33). That is the model's
  stochastic harmonic noise source, which kokoro.cpp documents as expected.
- PS5 risks retired early: kokoro.cpp needs only `espeak_Initialize`, `espeak_SetVoiceByName`
  and `espeak_TextToPhonemes`, all three already defined by `vendor/lib/libespeak-ng.a`, and
  its CMake takes `ESPEAK_NG_INCLUDEDIR`/`ESPEAK_NG_LIB` for a cross build.

## 2026-10-12 — Kokoro now runs on ggml/Vulkan: 130.5 s to 0.26 s

- Replaced the prebuilt OpenCL speech archive with kokoro.cpp built from source against the
  same pinned ggml and Vulkan backend as text. New `src/kokoro_runtime_ps5.cpp` implements
  `ps5_kokoro_tts_generate` over kokoro.cpp's C ABI, writes the 16-bit mono WAV itself and
  points espeak-ng at the model folder's `assets` through `KOKORO_ESPEAK_DATA_PATH`.
- `tools/vulkan-runtime/CMakeLists.txt` adds kokoro.cpp next to llama and stable-diffusion.
  It reuses the `ggml` target already created there, so there is still one ggml ABI and one
  Vulkan backend. Cross-build details: `HWY_COMPILE_ONLY_STATIC` with AVX2/FMA/F16C, because
  Highway's AVX-512 paths do not compile for `x86_64-sie-ps5` and the console is one fixed
  CPU; and an explicit imported espeak-ng target, because pkg-config would otherwise hand the
  cross compile the host's `/usr/include`.
- `tools/build-vulkan-native.sh` links `libkokoro.a` and `libhwy.a` instead of
  `libkokoro-tts.a`, and no longer rewrites that archive's backend-reservation symbol.
- ELF foundation unchanged: entry `0x120`, 14 program headers, the same 15 NEEDED PRX modules
  as the verified build, with no new dependency.
- Measured on `PPSA99023`, same sentence as the earlier profile, 78600 samples at 24 kHz
  (3.275 s of audio): cold `0.612214` s, warm `0.258404` s, backend reported as `Vulkan`,
  459 tensors loaded. The prebuilt archive needed `130.5` s warm for the same sentence, so
  this is roughly a 505x reduction and 12.7x faster than real time.
- Output is correct, not just fast: the console WAV and the host CPU reference have identical
  rate, channels, width and sample count, peaks of 13259 against 13468, RMS 1063.4 against
  1061.6 and frame-envelope correlation `0.9985`.
- Regressions checked after the link change: Mistral 7B Q4_0 decode `66.8337` tokens/s (same
  band as before) and SD-Turbo `30.4` s, matching its `30.324` s baseline. The OpenCL shim
  stays for image and audio; speech no longer touches it (`calls=0 cpu_kernel_calls=0`).
- Not done yet: the download catalog still points at the old Adreno weights. A fresh install
  does not fetch `kokoro-82m-f16.gguf` and `kokoro-voice-af_heart.gguf`; they were uploaded by
  hand for this test. `vendor/lib/libkokoro-tts.a` is now unused but still in the tree.
- Evidence: `performance/2026-10-12/kokoro-ggml-vulkan.json`.

## 2026-10-12 — the speech download catalog, and two installer bugs it exposed

- The Kokoro preset now installs the ggml runtime's own files: `kokoro-82m-f16.gguf` and
  `kokoro-voice-af_heart.gguf` from `simonfxr/kokoro.cpp-GGUF` at revision
  `c64650d0950e0b718b6eeb2f18620e25daf5c90b`, while the espeak-ng dictionaries and the licence
  set stay in the existing pinned repository. The dead Adreno files (`weights/model.fp16.bin`
  at 163.5 MB, the OpenCL kernels, `model.json`, the old voice pack and phoneme vocabulary)
  are gone from the catalog, so the bundle is 165.2 MB of files the runtime actually reads.
- `PresetFile` gained optional `repository`/`revision`, null unless a file comes from a
  different repository than its preset. One speech preset can therefore span the two sources,
  which the user agreed to, instead of mirroring the GGUFs or bundling dictionaries in the app.
- Installing it on the console failed the first time: "Cannot install completed bundle:
  Directory not empty (66)". `rename()` cannot move the staging folder onto an existing
  bundle, so any upgrade of an installed preset was impossible, not just this one. The
  installer now moves the old bundle aside, swaps the verified new one in, and puts the old one
  back if that fails.
- The swap then left `kokoro-82m-fp16.replaced` behind with the whole 163 MB of old weights:
  `remove_tree` unlinked entries while the directory's own stream was open, which leaves
  entries behind on this filesystem. It now reads the directory into a list first and deletes
  afterwards.

## 2026-10-12 — speech now faults on the console, and what is ruled out

- After the preset switch, every speech request on `PPSA99023` ends in `SIGSEGV` on the
  `prosperoai-http-conn` thread, right after kokoro.cpp logs the phonemes and builds its first
  graph (`bert_predictor.graph_build` completes, the next bench stage never prints). The same
  binary and the same model files produced five correct runs earlier in the day.
- Ruled out by measurement, each with its own console run:
  - the model files: the GGUFs on the console hash identically to the local copies that worked;
  - the amount of free memory: `kokoro_memory free=7385 MiB total=11712 MiB` right before the
    fault, and the fault also happens after loading only a 0.8B text model;
  - a resident text model: it faults with no text model loaded at all (no `model ready` line);
  - the Vulkan backend: it faults the same way with `KOKORO_BACKEND_CPU`;
  - holding the ggml-vulkan device for the process lifetime (an extra backend reference taken
    at startup, before any model);
  - `GGML_MAX_NAME=160`: a host build with the console's ABI synthesizes correctly on both CPU
    and Vulkan;
  - my own later edits: the exact code of commit `16d47e2`, which produced the five good runs,
    faults as well once the new preset is in place.
- The one measurement that separates the working session from every run since: the title had
  `11439 MiB` as its largest free block then, and has a stable `7385 MiB` now, across fresh
  launches with nothing loaded. About 4 GB of direct memory is no longer available to the
  title, which points at system state outside the process (the repeated faults and the two
  165 MB installs happened in between) rather than at the speech code.
- Next step is a console reboot to test that, since nothing in the process explains a missing
  4 GB. A reboot needs the user's approval, so it has not been done.

## 2026-10-12 — reboot recovery and Vulkan Kokoro preset

- The user rebooted the console. FTP was available; `ps5vkctl` and klog needed their ELF payloads
  resent before the test. The test title `PPSA99023` was stopped and unmounted before updating.
- A new eboot was built and deployed to the test title. On a fresh app start, Kokoro generated
  valid mono 24 kHz/16-bit audio repeatedly; the latest fresh run took `0.280395` s for
  3.275 s of audio. The resulting WAV had 78,600 frames and peak 13,259.
- A successful Mistral request followed by Kokoro still crashes the app during Kokoro graph
  execution (`curl` sees an empty reply because the handler process exits). Thus a fresh-start
  pass does not establish safe text-to-speech model switching; capture process state before
  interpreting any subsequent connection refusal.
- Renamed the catalog identity to `kokoro-82m-vulkan`, regenerated the preset header and updated
  the benchmark/test references. The former preset is no longer offered. Its old installed
  directory remains untouched and is explicitly excluded from text GGUF discovery.

## 2026-10-12 — cross-media switch cycle and PNG output work

- Rebuilt and deployed the current Vulkan source to `PPSA99023`, installed the pinned
  `kokoro-82m-vulkan` preset, then ran the HTTP media harness. Text → SD-Turbo → text passed
  (valid 512×512 TGA, 30.395 s); text → Stable Audio Open Small → text passed (valid stereo
  44.1 kHz/16-bit WAV, 524,288 frames, 151.4 s).
- The following Kokoro Vulkan request closed the HTTP connection and the app process exited
  (`agent procs` was empty). This is reproducible after prior text, image and audio requests;
  no Kokoro artifact was produced. A later klog window contained only system lines, not the
  app trace. A persistent debug-log switch was placed in the test app's logs folder for the
  next successful launch.
- The runtime design uses a single process-wide `inference_mutex`; native UI and web requests
  share the same selected-model router, and transitions release the previous text/media model
  before preparation. This establishes serialization by code inspection, not successful
  repeated-switch verification; the Kokoro crash remains a release gate.
- SD now writes a PNG plus its existing TGA preview using the PNG encoder already present in
  the Stable Diffusion archive. The API serves PNG with `image/png`; web rendering prefers the
  PNG, while native preview/archive compatibility remains on TGA. Native build and PNG path
  parser test pass. Console PNG generation is not yet verified because relaunch attempts now
  return `0x80940033`.
- Host native-controller and session-store tests pass with `ASAN_OPTIONS=detect_leaks=0`;
  LeakSanitizer itself is unsupported under this environment's ptrace. The existing browser
  history test still has two unrelated harness failures (`closeHistory` missing and a catalog
  count mismatch).

## 2026-10-12 — Kokoro scheduler cap restores complete modality switching

- The first Kokoro BERT predictor graph was crashing in `ggml_backend_sched_alloc_graph` after
  a successful text request. Vulkan heap trimming was ineffective (reported free memory stayed
  at 7379 MiB), and a CPU-only control still crashed, ruling out a Vulkan-memory-only cause.
- Capping Kokoro's scheduler graph and matching metadata reservation from 262144 to 131072
  nodes fixed the failure. The change is applied by the guarded PS5 adapter
  `tools/prepare-kokoro-ps5.py` during `build-vulkan-runtime.sh`, not by relying on an edit in
  the downloaded source tree.
- Final clean PS5 build passed a full API cycle on `PPSA99023`: text → SD-Turbo → text → Stable
  Audio Open Small → text → Kokoro Vulkan → text. SD returned a valid 512×512 PNG (601520
  bytes) alongside its TGA; Stable Audio returned stereo 44.1 kHz PCM (524288 frames); Kokoro
  returned mono 24 kHz PCM (52200 frames). Every text round-trip passed. Stable Audio took
  150.205 s, SD 29.764 s, and Kokoro 3.966 s. No samples clipped.
- The runtime libraries and native app folder build completed; the PNG parser test and
  `git diff --check` passed. The final deployed test title remains running on `PPSA99023`.
- Upstream review: llama.cpp `v0.6.0` (`d812350`, ggml `v0.26.0`) is the latest release as of
  this entry; our shared pin is `cb7934c` / ggml `v0.21.0`. Upgrade not yet attempted. Preserve
  upstream behind adapters and verify supported ggml operations per backend; HIP's generic AMD
  listing is not evidence of a PS5 ROCm/HIP runtime.

## 2026-10-12 — Stable Audio 3 PS5 C API smoke launch

- Built the pinned audio.cpp SA3 C API/runtime and ggml Vulkan static archives, then linked a
  minimal `audiocpp_registry_create` test with ProsperoAI's native RADV recipe. The PS5 native
  converter accepted the PIE and emitted `eboot.elf`; NEEDED includes the RADV PS5 winsys' AGC
  platform-driver imports. This is not the retired AGC inference backend.
- Backed up the registered, folder-backed `PPSA99023` eboot, confirmed `mounted:false`, replaced
  only `eboot.bin`, and launched via `ps5vkctl` (`0x00000018`). The user observed that the app
  crashed; immediately afterward `agent procs` was empty and ShadowMount reported `mounted:false`.
  Restored the exact original eboot and verified the title is unmounted with no process running.
- The klog receiver was unavailable (`3232` connection refused); the documented console
  `/data/payloads/klogsrv-ps5.elf` did not exist. The ps5vkctl log records only the successful
  launch call, so it does not distinguish a pre-main crash from a failure during registry
  initialization. No checkpoint was loaded and no SA3 inference was attempted. Next diagnose the
  PS5 runtime/import failure with klog available before making another launch attempt.

## 2026-10-12 — restored test title boots

- After restoring the saved PPSA99023 `eboot.bin`, the user launched the title successfully.
  `ps5vkctl agent procs` reports PID 111 and `shadow info` reports `mounted:true`. The console
  eboot size (114,253,716 bytes) matches the saved original exactly. This confirms the restored
  app boots; it does not change the failed SA3 smoke result because the smoke eboot was no longer
  installed for this launch.

## 2026-10-12 — SA3 staged load and close diagnostics

- Added compile-time `SA3_SMOKE_STAGE` to the PS5 C API harness: stage 1 initializes only the
  audio.cpp registry; stage 2 loads the SA3 GGUF; stage 3 creates the Vulkan session; stage 4
  generates audio. Each successful stage logs a marker and idles with `sceKernelUsleep` rather
  than a tight `pause` loop. Stage 1 ELF passed the RADV/native conversion and signed ELF checks.
- Restored the saved original eboot to `PPSA99023` and verified the remote SHA-256 exactly matched
  the local backup. Stage 1 was then deployed to this folder-backed test title; its remote file
  hash matched the local signed binary. Launch reached the registry-only ready marker. Stopping
  via `ps5vkctl agent kill` yielded normal ShellCore suspend, process exit, `end_app_mount`, and
  `mounted:false`; no kernel failure was seen at this stage.
- Deployed stage 2 and verified the remote SHA-256. Klog confirmed the call to load the 1.68 GB
  Stable Audio 3 Q8 GGUF, but no model-loaded marker followed. Kernel output reported `LOW FMEM`
  (about 1,000 free system pages), swap increasing beyond 700 MB, and ShellCore main-thread
  heartbeat freezes. A subsequent `agent procs` request stopped responding. No Vulkan session or
  inference was reached. Stage 2 remains installed on PPSA99023; original eboot backup remains
  unchanged in `build/audio-cpp-sa3-ps5-link-smoke/eboot-original.bin`. Wait for console recovery,
  confirm `mounted:false`, then restore the original eboot before further runs.
## 2026-10-12 — Stable Audio GGUF bounded reads

- Reproduced the host PS5 adapter path with an explicit streaming-GGUF define. The GGUF source
  now retains metadata and reads each tensor range with `fseeko`/bounded `fread` rather than
  retaining a checkpoint-sized `mmap`; single tensor reads above 256 MiB fail before allocating.
- Regression executable passed exact tensor bytes, release and reread, truncated-file rejection,
  oversized sparse fixture rejection, and metadata-only load of the 1.68 GB Stable Audio model.
  Process peak RSS was 116,208 KiB.
- Rebuilt and signed PS5 stage-2 diagnostic eboot. Deployed to the unmounted PPSA99023 folder
  title, chmod 755, downloaded it back and verified SHA-256 `2c910464fe9c43dc164b93ec6e90ee794705014b301e3fc81005e6583abc488a`.
- Console control payload had to be reloaded after it was absent; afterwards it reported healthy.
  PPSA99023 remains unmounted while the unrelated ProsperoEden PPSA99008 is active. No launch or
  PS5 runtime verification was done in this run. Await approval before switching titles.

## 2026-10-12 — Stable Audio 3 Vulkan inference verified on PS5

- Raised the bounded per-tensor read limit from 256 MiB to 512 MiB after the stage-4 attempt
  correctly rejected the largest tensor. The largest SA3 tensor is 393,216,000 bytes (375 MiB),
  so 512 MiB provides bounded headroom; this limit is per tensor and is unrelated to the PS5's
  available VRAM capacity. Host tests pass for exact reads, release/re-read, truncation, oversized
  sparse tensors and SA3 metadata load; peak RSS stayed below 128 MiB.
- Rebuilt, linked and signed the stage-4 PS5 harness, deployed it to the folder-backed test title
  PPSA99023, and launched it. Klog confirmed `generating on Vulkan device 0`, then
  `AUDIO_OK rate=44100 channels=2 frames=44100 peak=0.3198` and the ready marker indicating the
  WAV was written and the runtime released. This verifies a non-silent one-second stereo 44.1 kHz
  output generated by Stable Audio 3 on the console Vulkan path.
- A subsequent `agent procs` check confirmed PPSA99023 was running (PID 99). After collecting the
  result, stopped the test process cleanly and confirmed `mounted:false`. Restored the original
  test eboot and downloaded it back; its SHA-256 matches the preserved local original
  (`298326198b243711adae6476d7dbebb0c04e7d8e461cfb0639a18686b65a75cc`).

## 2026-10-12 — SA3 HTTP-thread teardown fix and randomized modality cycles

- The integrated SA3 request generated and sent a valid WAV, then the title received SIGSEGV as
  its short-lived HTTP connection worker exited. Added temporary checkpoints around adapter
  return, response serialization/send and worker teardown; the last completed checkpoint was
  HTTP response sent. This isolated the failure to post-response thread teardown, not Vulkan
  generation or WAV writing.
- The pinned audio.cpp C API has a `thread_local std::string` error value; its model-spec package
  cache also contains non-trivial TLS objects. The PS5 adaptation converts both groups to static
  state. ProsperoAI serializes audio.cpp calls and model loads with its inference mutex. The patch
  is reproducible in `tools/prepare-audio-cpp-sa3.py` and limited to the pinned dependency source.
- With the TLS adaptation, two repeated SA3 requests plus text → SA3 → text passed; the title
  remained alive. Then two full randomized cycles passed on PPSA99023, each with repeated requests
  per media model and text round-trips:
  1. Kokoro → text → SD → text → SA3 (19.15 s cold, 5.84 s warm) → text.
  2. SA3 (19.15 s cold, 5.84 s warm) → text → SD → text → Kokoro → text.
- SD generated valid 512×512 PNG/TGA, Kokoro generated valid mono 24 kHz PCM16 WAV, and SA3
  generated valid non-silent stereo 44.1 kHz PCM16 WAV. The repeated outputs were deterministic.
  JSON evidence: `vulkan-runtime-random-cycle-1.json`, `vulkan-runtime-random-cycle-2.json`, and
  `vulkan-runtime-sa3.json` under `docs/performance/2026-10-12/`.
- Console stayed responsive; no reboot was performed. Temporary diagnostic logging is being
  removed from the final build. The test title PPSA99023 was the only title changed; PPSA99004
  was not touched.

## 2026-10-12 — PR #27 downloader and curated-model console follow-up

- On PPSA99023, the same 4.03 GB Granite GGUF completed and passed downloader verification with
  8 and 16 parallel HTTP range connections. Progress samples were about 28–30 MiB/s at 8 and
  37–42 MiB/s at 16; a repeated 16-connection download confirmed the faster range. Four
  connections were observed around 17–18 MiB/s in an earlier interval. These are console API
  polling estimates, not controlled LAN throughput measurements.
- A 32-connection build was launched and a Granite download requested. The app exited before a
  reliable progress/resource sample; klog recorded `CRASH KILL_END` for PPSA99023. No exact cause
  was established. The shsrv monitor on port 2323 refused connections, so CPU and peak RSS at 32
  could not be measured. The final downloader is set to 16, the highest count with two complete,
  SHA-verified model downloads.
- Granite 4.0 H-Tiny loaded on Vulkan (weights 3,875 ms). Three identical 40-token warm answers
  were correct; runtime elapsed times were 501,326 / 502,371 / 504,255 us (median 502,371 us,
  about 79.6 generated tokens/s). Before switching to Granite, the console reported a 6,099 MiB
  largest free block and 11,712 MiB Vulkan heap. Granite was removed after the test.
- ACE-Step 1.5 passed its first real-model PS5 generation after the per-tensor read bound was
  raised to 1 GiB. HTTP returned 200 in 118.282 s and the output is an 8-second, stereo 48 kHz
  PCM16 WAV; its bytes and SHA-256 are stored in
  `work/audio/ace-step-1.5-ps5-2026-10-12.wav`. Qwen 9B then switched back and answered “4” in
  4.286 s. Machine-readable evidence: `performance/2026-10-12/pr27-console-followup.json`.
- The final PPSA99023 app is running the 16-connection build with Qwen 9B selected. Granite was
  deleted after benchmarking. ACE-Step remains installed as the curated preset. PPSA99004 was
  not touched; the console was not rebooted.

## 2026-10-12 — PR #27 workspace export and idle-release follow-up

- Text export now writes a plain `.txt` artifact and session reference; the Workspace saves the
  conversation before inference so new-session outputs have a valid reference. A console API
  request returned the text through the HTTP media route as `text/plain`; deleting the temporary
  test conversation removed its exported file.
- Media archives keep their original PNG/WAV files and session references without writing
  per-output JSON sidecars. Host session-store tests confirmed that two outputs at the same
  message index get different filenames and both remain present. PS5 returned 200 with
  `image/png` and `audio/wav` for existing archived outputs.
- Setting idle unload to one minute released Qwen after 60 seconds in klog. A subsequent request
  reloaded the model and returned HTTP 200 in about 4.1 seconds. The setting was restored to two
  minutes.
- Tests: `tests/test_session_store.py`, `tests/test_http_api.py`, `make format-check`, and
  `git diff --check` passed. The final build was deployed to PPSA99023; Qwen remains selected.

## 2026-10-13 — PR #27 storage comparison and SDXL memory check

- Retried FTP to PPSA99023 successfully after stopping the external-storage benchmark build and
  confirming the title was unmounted. Restored the internal-storage build and launched it; the
  app remained alive during the following tests.
- Qwen3.5 0.8B warm requests measured 0.117–0.121 s from both internal storage and
  `/mnt/ext1/prosperoai`. Kokoro `af_heart` warm medians were 0.301 s internal and 0.303 s
  external. Cold samples include app/model setup. No meaningful warm inference speedup was found
  from external storage. Test assets remain in the requested external directory. Samples and WAVs:
  `performance/2026-10-13/pr27-storage-comparison.json` and `../work/voice/`.
- The corrected 6,938,078,334-byte SDXL checkpoint downloaded at about 33 MiB/s and passed bundle
  verification. Generation failed on Vulkan workspace allocation: with a 11,712 MiB heap and
  6,450.88 MiB tracked weights, the UNet needed 843.51 MiB device / 331.51 MiB budget and had no
  available allocation. The retry failed while preparing `unet.input_blocks.1`. Do not report
  SDXL as usable on this PS5 profile.
- FLUX.2 Klein's Q4_K_M encoder likewise could not allocate about 5.94 GB with a 5,245 MiB largest
  free block. A smaller encoder remains to be evaluated. Machine-readable results are in
  `performance/2026-10-13/pr27-storage-comparison.json`.
- PPSA99023 stayed running on the restored internal build. PPSA99004 was not touched and the
  console was not rebooted.

## 2026-10-13 — PR #27 12B MoE and allocator memory follow-up

- Added the experimental BoHF 12B MoE 3A Q3_K_M preset from the author's GGUF repository,
  pinned at `a0ab1f1455f4bad47f31c8c52f6aec1569699da5`. PPSA99023 downloaded 4,703,718,432 bytes
  and passed the preset SHA-256 check. The standard `llama` architecture loaded and generated
  `84 × 3 = 252`; a warm 48-token response took 724.202 ms of decode time, about 66.28 tokens/s.
- During the run, klog recorded release of the idle SDXL context after 120 seconds. The next
  model-change sample reported a 9,839 MiB largest free PS5 direct-memory block and Vulkan
  reported 10,317,922,304 bytes free of a 12,280,922,112-byte heap (11,712 MiB total). The prior
  FLUX failure reported 5,245 MiB largest direct-memory block while trying a 5,939.93 MiB CPU
  buffer. These values show that the 5 GiB observation was state-dependent; they are not the same
  metric as the full Vulkan heap. Releasing the idle SDXL context is a practical memory recovery
  path. There is no allocator setting that can raise the OS/driver-reported Vulkan budget.
- The title PPSA99023 remains running with BoHF selected. No reboot or change to PPSA99004.
  Machine-readable evidence: `performance/2026-10-13/pr27-bohf-12b-moe.json`.

## 2026-10-13 — PR #27 preset and KV-cache verification

- PPSA99023 verified short text generation for Mistral 7B Instruct Q4_0, Qwen3.5 9B Q4_0,
  MiniCPM5 2B Q8_0, Ternary Bonsai 2 27B PTQ1_0, Granite 4.0 H-Tiny Q4_K_M and BoHF 12B MoE
  3A Q3_K_M. Granite downloaded 4,230,976,352 bytes, passed preset integrity verification, and
  answered `84 multiplied by 3 equals 252`.
- Existing console runs verified SD-Turbo, Stable Audio 3 Small SFX, ACE-Step 1.5 Turbo and
  Kokoro. New retries still failed for FLUX.2 Klein (Qwen encoder could not allocate a 5,939.93
  MiB CPU buffer) and SDXL (sampling needed 636.55 MiB of Vulkan device workspace and reported
  zero available). Both remain unusable on this PS5 profile.
- `inference/adapters/llama/vulkan.cpp` already uses llama.cpp KV memory for token-by-token
  decoding and implements a longest-common-prefix reuse path for repeated prompts. Three
  identical Qwen HTTP calls returned the expected answer but logged `reused=0` each time, so
  cross-request reuse is not proven on the live console. The Hugging Face article's general
  `use_cache=True` behavior is already provided by llama.cpp; improving prefix reuse needs a
  separate investigation. No cache code was changed.
- PPSA99023 remained running. PPSA99004 was not touched; no reboot.

## 2026-10-13 — PR #29 PS5/PS5 Pro tuning profile A/B

- Added explicit `ps5` and opt-in `ps5pro` app profiles plus guarded environment overrides in
  the pinned ggml-vulkan source. The existing PS5 mat-vec tuning remains the default; the Pro
  profile uses `GGML_VK_PS5_DMMV_WG=1`, subgroup 32, and batch/ubatch 512. A configurable
  `vulkan_env.txt` applies individual experiment overrides after profile defaults.
- Built and deployed the profile-enabled app only to the unmounted folder-backed test title
  PPSA99023. With Mistral 7B Instruct v0.3 Q4_0, context 4096, batch/ubatch 512 and 256 output
  tokens, five-run medians were 72.27 tok/s on `ps5`, 69.17 tok/s with the PR #29 Pro values,
  then 72.31 tok/s after returning to `ps5`. The paired base profile readings are stable; the
  Pro values are about 4.31% slower on this base PS5. These results do not measure a PS5 Pro.
- PPSA99023 remains running with the `ps5` profile. PPSA99004 was not changed; no reboot.
  Full reports: `performance/2026-10-13/pr29-ps5-base-profile.json`,
  `pr29-ps5pro-profile-on-ps5.json`, `pr29-ps5-base-profile-after.json`, and
  `pr29-ps5-profile-comparison.json`.


## 2026-10-14 — PR27 recording, web fixes and allocation investigation

Added DualSense/web voice recording, native glyph controls and confirmed L3 deletion for
own/imported voices. User confirmed console capture works. Browser integration validates
capture/save, Chatterbox voice changes and deferred model selection with one preparation
on first typing. History tests confirm deleting the active chat opens a fresh chat.
Host HTTP API tests: 22 pass; console tool tests: 5 pass. Native app builds and loader ELF
validation pass. Deployed `pr27-web-lazy-load` to stopped/unmounted PPSA99023; process
PID 161 and direct app API verified. Live voice setting changes and deferred selection
pass; original voice restored. Main title unchanged, no agent-initiated reboot.

S3/HiFT/T3 metadata arenas are bounded by pinned source preparation. Engine session reuse
and replacing thread-local catalog storage eliminate observed allocation/teardown errors
in latest runs. Speech quality still fails: Cemo 20 ms silence, custom 40 ms output.
The custom reference is valid 5-second 16 kHz mono speech. Structural WAV success is not
speech success; the custom report now explicitly records quality failure. T3 early EOS
requires further diagnosis. Full model matrix remains unverified.


### 2026-10-14 — backend memory indicator and T3 KV initialization

Added actual backend load/release notifications and `/api/models/memory`. Web polls the
state below PS5 status, native reads it in the persistent upper-right header, and the
native workspace uses the loaded name instead of "Choose model". Live checks confirm
prepare from web, switch/unload and deferred selection. HTTP API tests now 23 pass;
router residency and browser integration pass. PPSA99023 runs `pr27-memory-zero-kv`,
PID 173. Main title untouched.

Official Chatterbox Python generation and T3 source were reviewed. Matching default
sampling parameters did not provide a setting-only fix. T3 runner copied only the
active KV prefix, leaving masked tail values uninitialized. Guarded preparation now
zeros the full cache capacity on import. Cemo generates 15.34/12.66-second non-clipped
WAVs; custom cold fails, repeat generates15.36 seconds. Speech intelligibility and EOS
correctness remain unverified; long WAVs from a short fixture are suspicious. Short
outputs under250 ms are rejected. Evidence is in the dated zero-kv reports/token trace.


### 2026-10-14 — listener confirmation

Saved Cemo tests to shared conversation `test-chatterbox-cemo-20261014-kv`; live browser
verified two playable audio references. The user confirmed speech is unintelligible.
Chatterbox remains a failed quality gate despite longer non-clipped WAVs. Explicit F32
decode attention compiled as a diagnostic; console verification remains pending.

### 2026-10-14 — Chatterbox T3 fixed on PS5 (explicit F32 decode attention)

Host reference with the same pinned GGUF and prepared audio.cpp: "Hello from Prospero AI."
gives 1.78 s on CPU and 1.74 s on host Vulkan (Intel HD 630). The console produced 12–15 s
unintelligible audio with flash attention, so the fault was PS5-specific. The earlier
`pr27-explicit-attention` run (all-NaN decode logits) used a stale eboot built before the
final source preparation. ggml-vulkan environment switches (fusion, MMVQ/integer dot) did
not change that build. `GGML_VULKAN_CHECK_RESULTS` crashed in `ggml_vk_compute_forward`
(null subcontext) and, once guarded, was too slow to reach T3; it was reverted.

A clean rebuild with explicit F32 QK^T/softmax/V attention (`pr27-t3-clean`) on PPSA99023:
demo_1_man 2.14 s (EOS at step 54), Cemo 2.06 s (step 52), Cemo 19-word sentence 5.58 s
(step 140); warm generation 1.4–3.1 s, no clipping. The user listened in conversation
`test-chatterbox-fixed-20261014` and confirmed it works. Report:
`performance/2026-10-14/pr27-chatterbox-t3-explicit-attention-clean.json`. A same-build A/B
against flash attention was not repeated. Added `ps5ctl.py trace` for the app debug trace;
`build-vulkan-native.sh` no longer needs `rg` and its symbol checks survive `pipefail`.
Main title unchanged, no reboot.

### 2026-10-14 — model switching: Granite, BoHF, Chatterbox and ACE-Step

Correction to the entry above: the failing `pr27-explicit-attention` run was not a stale
build. Chatterbox failed whenever llama.cpp had used the Vulkan device earlier in the process
(fresh → Chatterbox passes; after MiniCPM/Mistral/BoHF it returns NaN decode logits). A T3
node probe showed the prefix-cache K/V input to the first prefill CONCAT was entirely NaN:
the decode runner imports an empty cache for the prefix pass and attention reads masked,
never-written slots; reused PS5 direct memory is not zeroed, and 0 × NaN propagates.
`RADV_DEBUG=zerovram` did not change it. Fix: clear both T3 runner buffers after allocation
(`prepare-audio-cpp-sa3.py`). Flash attention is then correct too, but explicit F32 attention
decodes a 19-word sentence in 3.15–3.5 s versus 4.4–5.2 s, so it stays.

Granite 4.0 H was refused because `llama_model_n_head_kv()` reports layer 0, a recurrent
layer with 0 KV heads, giving a 0 MiB KV estimate. Hybrid models now estimate with all
attention heads. BoHF and Bonsai were refused after other models because the KV budget used
`sceKernelAvailableDirectMemorySize`, the largest contiguous block: after the first load it
stayed near 7 GB (later 5 GB, even 304 MiB) while about 11 GB was free in total. Vulkan
buffers are at most 128 MiB (`suballocation_block_size`), so the adapter now budgets total
free direct memory (`include/direct_memory.hpp`, `sceKernelDirectMemoryQuery`). A refused
model is released immediately. Ten consecutive text switches across all six text models
passed; the router log now reports largest block and total free memory.

ACE-Step passed once per process, then failed allocating 76–133 MB with
`ErrorOutOfDeviceMemory` (a 128 MiB direct allocation probe still succeeded afterwards) or
crashed the title. `prepare()` loaded the planner LM, pre-DiT/text encoder, DiT and VAE
together: ggml-vulkan peaked at 7.39 GiB. With `mem_saver`, stages now load on demand and
the planner, pre-DiT and DiT are dropped after use (`adapt_ace_step_staged_memory`); the
peak is 4.53 GiB. Two consecutive ACE-Step runs and ACE-Step after Bonsai 27B and Qwen 9B
pass; a request takes about 132 s instead of 114 s. About 300 MB of direct memory per
ACE-Step request is still not returned after release (not ggml buffers).

First 3-round all-model benchmark before the Chatterbox/ACE fixes:
`performance/2026-10-14/pr27-all-models-3x.json` (10 of 12 models 3/3). Native About card
now credits BlackBearReloaded and Quatrixone. PPSA99023 only; main title unchanged; no reboot.

### 2026-10-14 — all models, 3 rounds, and PS5 Pro memory bit on a base PS5

`pr27-ace-staged` on PPSA99023: `tools/benchmark-all-models-ps5.py --rounds 3` passed 36/36
requests, each one a model switch, title alive throughout. Medians (HTTP time includes
loading): Mistral 7B 72.5 tok/s, Qwen3.8 9B 53.2, MiniCPM5 109.1, Bonsai 27B 13.2, Granite
105.0, BoHF 68.9; SD-Turbo 30.6 s, Realistic Vision 36.0 s, Stable Audio 19.2 s, ACE-Step
114.3 s, Kokoro 0.4 s, Chatterbox 14.4 s. Report:
`performance/2026-10-14/pr27-all-models-3x-final.json`.

ProsperoEden PR #139 sets `attribute3` bit 22 (PS5 Pro memory mode; +1,216 MiB direct memory
measured on a PS5 Pro). On this base PS5 the test title launched with `attribute3`
`0x500000` and still reported a 11,712 MiB pool, the same as with `0x100000`. The original
`param.json` was restored. Not tested on a PS5 Pro with ProsperoAI.
