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
  repackaged as `PPSA99023`. Result: identical `ProcessTerm()` crash, with not even the first
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
  (`PPSA99023`) while it was running. Identical crash. The launch necessarily killed Lapy first
  (one foreground app at a time), so this doesn't fully rule out Lapy mattering if it could run
  truly in the background, only that launching it immediately before doesn't help.

## 2026-10-08 — RADV and llama.cpp run on console; full app warmup fixed

Reference RADV linking, platform heap wrappers, SDK libc++18 and emulated TLS produced a working PPSA99023 smoke. Device discovery: RADV NAVI21, Vulkan 1.4.354. Old shell-exit logs prove earlier blanket pre-main crash interpretation incorrect: tests called sceSystemServiceLoadExec("exit", NULL), then the system failed executing "exit". A bounded console coredump watcher copied no dumps for that run.

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

User pointed out that RetroArch runs from a folder and that AGC should be the original upstream build. Confirmed RetroArch PPSA99169 source_type=folder. Downloaded official blackbearreloaded/ProsperoAI release 01.000.000 ZIP, verified published SHA-256 19c2bacebf6fb1587b32d458fb5af2fb472d9f3844085993b0f1a30669dc0c69. Copied its unmodified eboot.bin/libraries/assets to test slot PPSA99023, only changing param.json title/concept/content ID. Added recipe-verified AGC Mistral model and built-in auto_chat.txt prompt; no inference-code changes or HTTP additions to this baseline.

Folder launch initially refused 0x80aa001a with errno=13. FTP SITE CHMOD 755 on eboot.bin and libc.prx fixed it; upstream title boots and generates coherent text. Three fresh-launch runs with default system prompt, 62 input tokens, 128 generated tokens and 4096 context capacity: decode 47.96, 47.92, 48.02 tok/s, median 47.96. Numerator is 127 decode steps, excluding the first token supplied by prefill, matching the upstream UI formula. build/benchmark-upstream-agc-mistral.json, /tmp/prospero-upstream-agc{2,3,4}-klog.txt and /tmp/prospero-upstream-agc-result.jpg preserve evidence. Load was 1.43–1.93 seconds from folder.

Earlier WIP API requests were sent while background loading was incomplete and failed at stage 2; a later captured warmup succeeded. Those failures are not evidence that upstream AGC is broken. Do not use their failed result JSON as a performance baseline. Temporary diagnostics in the AGC inference backend were reverted; baseline uses release binary.

Added app-vulkan-folder target and FTP put-dir/size/chmod commands. Folder upload applies 755 to executable/modules. Updating the stopped folder now needs only changed files; no multi-gigabyte repackaging. Vulkan being aligned to the same system prompt, 62 input tokens and 4096 context capacity before final comparison.

### Final comparison and console state

Vulkan aligned to context capacity 4096 and same default UI system/user text as upstream. Three fresh-launch decode results: 69.73, 69.72, 69.72 tok/s, median 69.72, versus upstream AGC 47.96. Same source Q4_0 model, 128 generated tokens. Prompt encoding differs by one token (AGC 62, llama.cpp 61); decode numerator is 127 after first prefill token. Vulkan +45.4% for this short-context decode test. Vulkan prefill ~0.343 s; reported load ~59.9 s vs upstream folder load 1.43–1.93 s. Main GPU buffers at context4096: 3850.02 + 512 + 52.01 MiB (~4.31 GiB). Full report: docs/VK_BENCHMARK_2026-10-08.md; JSON: build/benchmark-comparison-mistral.json.

Final console state: Vulkan folder /data/homebrew/PPSA99023, pid192 running. Temporary auto_chat.txt removed, so ordinary launches do not automatically benchmark. Original PPSA99004 untouched. No-change Vulkan folder build measured 3.776 s. Host integration tests 2 passed, shell syntax and Python compilation passed, benchmark arithmetic validated with mocked response including first-token accounting, git diff --check clean. No commit or push.

Final network verification: http://<PS5-IP>:11434/ returned HTTP 200; /api/tags exposes loaded Mistral GGUF. Final screenshot confirms llama.cpp Vulkan / RADV, model loaded, HTTP server listening.

## 2026-10-08 — PR scope cleanup

Removed the Models/Settings tab redesign, model-delete UI/API and shared deletion helper from the branch diff at the user’s request. Restored the original Conversation/Workshop console layout and pre-redesign web chat. Retained HTTP networking, generation timing and the 8 MiB warmup stack fix. The rebuilt Vulkan folder compiles, links and signs; Python tooling tests pass. Hardware performance figures above apply to the previously tested binary; the UI-restoration rebuild has not been redeployed.

## 2026-10-08 — OpenAI-compatible API and OpenCode

Added Vulkan /v1/models and /v1/chat/completions, SSE text deltas, usage/finish reasons, optional bearer key on all routes, bounded request parsing, worker joining and configurable context (512–16384; default 4096). Tools use a portable schema prompt and GBNF envelope/name constraints with OpenAI tool-call IDs and tool-result history; this is not native Jinja tool templating or full argument-schema validation.

PS5 PPSA99023: /v1/models/auth, non-streaming completion, SSE usage/DONE and required lookup(query=Vulkan) followed by tool result and final answer verified. OpenCode 1.1.36 direct streaming returned API_OK. Automatic read-tool choice on Mistral 7B hallucinated file contents; do not claim reliable coding-agent behavior. API tests used context_size.txt=16384, separate from the 4096-token benchmark. Temporary test key stored only outside Git. Results: /tmp/prospero-openai-console-results.json and /tmp/prospero-opencode-test/. Existing AGC build retained for media; image/audio/speech Vulkan migration planned. User requested akandr/bc250 reference as related Linux APU background, not PS5 evidence.

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
  PPSA99023 (PID 95) to launch it. First shadow delete stopped with EBUSY (16);
  retry after closing PPSA99023 completed, source absent before upload.
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
to PPSA99023 completed. Full-app model ready2908.83 ms at installed context16384;
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

Closed PPSA99023 temporarily; old PPSA99019 benchmark replacement initially
returned EBUSY until active app closed. Verified registration404 and source
absence before uploading the20MiB UI probe. Automatic rescan registered it.
Launch0x0000c018, PID118: all Vulkan init/acquire/submit/present calls return0;
120 frames presented and process stayed alive. Source report saved at
build/vulkan-ui-probe/console-results.txt; klog /tmp/prospero-ui-probe-klog.txt.
No screenshot: active Remote Play manager/session unavailable. API success does
not prove pixel correctness, UI parity, text rendering or inference concurrency.
Probe then closed and optimized legacy PPSA99023 relaunched. No reboot or main
PPSA99004 changes. Migration stages are documented in VK_UI_MIGRATION.md.


## 2026-10-09 — Native Models presets, HTTP workspace and hybrid AGC Voice

- PPSA99023 only: updated artwork/splash and Models trigger navigation; Triangle Space,
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
- Final web assets uploaded only after PPSA99023 reported `mounted:false`; title
  relaunched. Live browser test against the console HTTP server passed: Speak selects
  Kokoro, Back preserves history, current-conversation deletion returns to Workspace,
  and empty Image opens the SD-Turbo preset. Screenshot `build/prospero-web-live-test.png`.
  Final native rebuild reproduced the same binary SHA-256. Test title remains running.
