# PS5 model loading — 2026-10-09

PS5 FW 12.70, Mistral 7B Instruct v0.3 Q4_0, pinned llama.cpp
cb7934c52ca8710994b2ecc19775ebefcfdb8d01. Original PPSA99004 untouched.

## Results

| Configuration | Weights median | Context median | Model ready median |
| --- | ---: | ---: | ---: |
| Raw GGUF, serial reads, full debug logging | 48.945 s | 8.091 s | 57.036 s |
| Raw GGUF, parallel reads, full debug logging | 7.523 s | 8.091 s | 15.614 s |
| Raw GGUF, serial reads, filtered logging | 44.183 s | 0.028 s | 44.212 s |
| Raw GGUF, parallel reads, filtered logging | 2.740 s | 0.028 s | 2.768 s |

Each row uses three measured loads in an alternating serial/parallel process,
context 4096, batch/ubatch 256, four CPU threads and all 33 layers offloaded.
All first greedy token checks returned token 29493 and result 0. This verifies
Vulkan output consistency between loading modes, not equivalence to AGC output.

Three subsequent fresh optimized process launches measured first model-ready
loads **2.92159 / 2.84294 / 2.83605 s**, median **2.84294 s**. First evaluation
was ~318 ms separately. The second report was collected after its second round;
its first-load measurement is complete, but it does not contain a COMPLETE marker.
A fresh process does not prove an uncached SSD; the console was not rebooted.

Unmodified official AGC 01.000.000 was independently launched three times with
its prepared model files in the same folder test slot. Its warmup `load_us` was
**1614361 / 1526112 / 1487587**, median **1.526112 s**. All three runs generated
successfully. AGC still loads faster. AGC prepared files and Vulkan raw GGUF use
different formats and their phase boundaries are backend specific. These numbers
measure model preparation, not total title launch. Prompts/templates differed;
no cross-backend token equivalence is claimed.

The isolated baseline-to-optimized improvement is about **20×**. The earlier
llama performance `load time` also included elapsed waiting until first eval,
so it should not be used as an isolated disk-read measurement.

## Changes and storage limit

Four parallel positional 2 MiB reads populate each 8 MiB staging buffer;
four buffers give a 32 MiB ring. Workers finish before reuse or fread fallback.
`PROSPERO_MODEL_IO_SERIAL` restores the original serial/1 MiB path for diagnosis.
The guarded PS5-only dependency edits are reproduced by
`tools/prepare-llama-ps5-io.py` during the library build.

Synchronous sceKernelDebugOutText forwarding of backend debug output added
seconds to loading and context creation. Default callbacks now retain WARN/ERROR
and explicit runtime summary messages. `/app0/vulkan_verbose_logging.txt` enables
full backend logging. mmap remains disabled after its earlier FMEM stalls.

A compressed ffpfsc containing this GGUF remained around 47 s for weights
(serial median 47.753 s, parallel 47.413 s). Do not promise this speedup for
compressed model packages; use an uncompressed folder GGUF. Allocation/clear
probes for 512 MiB took ~0.02/14.2 ms, excluding them as the 8 s context cause.

## Full application verification and evidence

The legacy RmlUi application was rebuilt from parent commit 8e1daee with the
corrected runtime and current llama archives. ELF NEEDED libraries, entry 0x120,
14 program headers and header types match the working build. Native OpenGL UI
and RADV combined linking remains a separate unresolved task.

User-authorized deployment to PPSA99023 completed. Full-app warmup reported
**device 4.16 ms, weights 2831.79 ms, context 72.87 ms, total 2908.83 ms**;
the installed context configuration remains 16384. It generated 25 tokens and
remained running (PID 116 at verification). Temporary automatic-chat and benchmark
configuration files were removed. Original assets were restored; exact original
binary and assets remain backed up in `build/agc-recheck/vulkan-backup`.
Optimized signed binary SHA-256:
`950d5e2d000cb664b51e529fcf5a4630fabb9a370515dfd15c576bf9a44a14c1`.

Local evidence (ignored build/scratch files):

- `build/agc-recheck/vulkan-folder-{verbose,quiet}-results.txt`
- `build/agc-recheck/vulkan-cold-{1,2,3}-results.txt`
- `build/vulkan-load-benchmark/console-results.txt` (compressed package)
- `/tmp/prospero-agc-recheck-{1,2,3}-klog.txt`
- `/tmp/prospero-fixed-app-klog.txt`
- `/tmp/prospero-load-legacy/build/prospero-vulkan/` (verified legacy artifact)

Host byte/cursor/short-read tests passed, including ASan/UBSan with leak detection
disabled under the sandbox. PS5 llama archives, isolated benchmark and legacy app
built and signed. Runtime syntax check and git diff whitespace check passed.
