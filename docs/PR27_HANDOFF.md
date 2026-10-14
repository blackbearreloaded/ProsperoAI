# PR27 continuation — 2026-10-14

## Scope and console rules

Keep the installed text/media models working on PS5 with the common upstream GGML/Vulkan
runtime. Chatterbox uses the pinned audio.cpp adapter and now exposes all supported languages
in the voice controls. Keep upstream pinned; Bonsai's narrow Prism quantization/Hadamard
compatibility is applied by preparation scripts.
Read AGENTS.md before console work. Set PS5_HOST and PROSPERO_TEST_TITLE in the environment;
the authorized folder-backed test slot is PPSA99023. Never overwrite PPSA99004 or reboot
without separate authorization. Task deployments, payloads and test launches are authorized.

## Verified Bonsai result

The retained PR performance record is
`docs/performance/2026-10-14/pr27-all-models-3x-final.json`; it includes three coherent
Bonsai runs and the final all-model benchmark. The profiling package marker
`vulkan_profile.txt` reduced performance to approximately 4.8 tok/s; it was removed from
the local package and the console test folder. Preserve that removal.

`tools/prepare-bonsai-llama.py` reproduces the console-tested model/graph/context source
from the pinned upstream checkout and is idempotent. See the dated reproducibility note.
Build the shared runtime with `bash tools/build-vulkan-runtime.sh ps5`, then the app with
`PROSPERO_APP_TITLE="$PROSPERO_TEST_TITLE" BUILD_LABEL=pr27-common-runtime bash tools/build-vulkan-native.sh`.
Dependency sources and build outputs are intentionally ignored; fetch/build scripts must
reconstruct them. The host runtime and PS5 runtime/app built successfully during this work.

## Chatterbox and recording — current result

Chatterbox works on PPSA99023, also after other models. Three PS5 changes in
`tools/prepare-audio-cpp-sa3.py`: the T3 decode cache zeroes its full capacity on import;
both T3 runner buffers are cleared after allocation, because the prefix pass reads masked,
never-written cache slots and reused PS5 direct memory holds stale data after llama.cpp
(0 × NaN made every decode logit NaN); and T3 decode attention uses explicit F32
QK^T/softmax/V, which is about 1.4× faster than `ggml_flash_attn_ext` here. The host
reference gives 1.74–1.78 s for "Hello from Prospero AI."; the console gives 2.06–2.46 s and
stops on EOS. The user listened in conversation `test-chatterbox-fixed-20261014` and
confirmed it works. Evidence: `pr27-chatterbox-t3-explicit-attention-clean.json` and the
2026-10-14 VK_LOG entries.

## Model switching — current result

Granite 4.0 H loads (hybrid KV estimate fixed). BoHF and Bonsai load after other models:
the llama adapter budgets total free direct memory instead of the largest contiguous block
(`include/direct_memory.hpp`). ACE-Step loads its stages on demand and drops each one after
use (Vulkan peak 7.39 → 4.53 GiB), so repeated runs and runs after large text models pass.
`tools/benchmark-all-models-ps5.py` runs every installed model for N rounds with a model
switch on every request. Intermediate performance reports were removed from this PR; the
retained final three-round record is `docs/performance/2026-10-14/pr27-all-models-3x-final.json`.

Native DualSense capture and web capture/save are implemented. The user confirmed PS5
recording works. The native voice picker has visible glyph explanations and L3 delete
with confirmation for own/imported samples. Browser capture requires HTTPS/localhost;
on LAN HTTP, the web recorder can use the PS5 microphone through the app API.
Physical persistent output storage is `/data/prosperoai/work/{image,audio,voice}`;
`/download0/ProsperoAI/...` remains the sandbox/HTTP alias.

Web runtime control initialization is fixed, including Chatterbox voice switching.
Chatterbox now offers all 19 supported speech languages in the web and native controls.
In the PS5 Workspace list, “New conversation” follows the voice and language settings.
Settings requests are serialized. Model selection sends `prepare:false`; first typing
prepares once, and submission waits for preparation. Deleting the current conversation
opens an empty chat instead of returning to welcome. Both headers show actual backend
residency through `/api/models/memory`; native workspace replaces "Choose model" with
the loaded model name. Live API verifies web preparation, switch/unload and deferred
selection. Native drawing reads the same runtime snapshot. Browser integration and host API,
recording, history and preparation tests pass.

`tools/ps5ctl.py` reads an ignored mode-600 `.env.ps5.local` (template `.env.ps5.example`).
All resident service ELFs, SHA manifests and licenses are in `tools/payloads/`;
`doctor` checks them and `service restore` restores missing listeners. P5 Manager is
optional. See updated AGENTS.md and BUILDING.md.

## Remaining failures and limitations

- About 300 MB of direct memory per ACE-Step request is not returned after the engine is
  released (not ggml-vulkan buffers). It no longer blocks ACE-Step but limits long sessions.
- The host UI preview aborts after the About screen; this predates the PR27 changes.
- Host tests that use AddressSanitizer cannot link on this host (missing LLVM 14 runtimes).

## Console state

Connectivity was restored after the user's reboot. FTP/control/klog/shell are available.
The authorized folder-backed slot is PPSA99023. The latest app build label is
`pr27-ace-staged`; main PPSA99004 was not changed and no reboot was requested.
Recheck process/mount state before subsequent deployment.
