# PR27 continuation — 2026-10-14

## Scope and console rules

Finish all installed text/media models on PS5 using the common upstream GGML/Vulkan
runtime. Chatterbox is the user's next priority. Keep upstream pinned; Bonsai's narrow
Prism quantization/Hadamard compatibility is applied by preparation scripts.
Read AGENTS.md before console work. Set PS5_HOST and PROSPERO_TEST_TITLE in the environment;
the authorized folder-backed test slot is PPSA99023. Never overwrite PPSA99004 or reboot
without separate authorization. Task deployments, payloads and test launches are authorized.

## Verified Bonsai result

`docs/performance/2026-10-14/bonsai-hadamard-no-profile.json` records correct arithmetic,
three coherent 94-token answers (13.2605, 13.4333, 13.2587 tok/s), Thinking mode and
a short 16K-context request. Median decode is 13.2605 tok/s. The profiling package marker
`vulkan_profile.txt` reduced performance to approximately 4.8 tok/s; it was removed from
the local package and the console test folder. Preserve that removal.

`tools/prepare-bonsai-llama.py` reproduces the console-tested model/graph/context source
from the pinned upstream checkout and is idempotent. See the dated reproducibility note.
Build the shared runtime with `bash tools/build-vulkan-runtime.sh ps5`, then the app with
`PROSPERO_APP_TITLE="$PROSPERO_TEST_TITLE" BUILD_LABEL=pr27-common-runtime bash tools/build-vulkan-native.sh`.
Dependency sources and build outputs are intentionally ignored; fetch/build scripts must
reconstruct them. The host runtime and PS5 runtime/app built successfully during this work.

## Chatterbox next

The current adapter is `inference/adapters/media/chatterbox_ps5.cpp`, calling upstream
audio.cpp with the existing `chatterbox-500m-q8-0` bundle. It now explicitly selects
Vulkan device 0, keeps mem_saver enabled and logs the selected backend. This change is
compiled only; it is not proven to fix generation. Previous CPU attempts aborted on
192–256 MiB arena allocation. Earlier Vulkan attempts also failed; collect a fresh log
and diagnose rather than assuming the backend switch is sufficient.

The latest signed app is under `build/vulkan-runtime/PPSA99023/`, SHA-256 of eboot.bin:
`3699cd7344d178397f6a3bf4448b97426cfde4ec68d5eaca067dc744011ec8e7`.
It has not been deployed. ELF dependency/header checks passed. The app also contains the
experimental static ORT entry point, but no ONNX Chatterbox adapter calls it. ORT is CPU
only on this port; the user requires GPU inference. The older ONNX checklist is research,
not evidence of a working replacement.

Restore console connectivity, check agent procs and ShadowMount registration/mount state,
stop the verified test title, confirm mounted:false, upload the folder with ftp put-dir,
launch and confirm the process before polling port 11434. Use the app HTTP API directly.
Require two successful Chatterbox outputs, WAV validation, memory behavior and a text
round-trip. Save generated media under work/voice on the console.

## Remaining failures and limitations

- The full Bonsai benchmark's subsequent model-switch phase crashed the title. The
  report proves Bonsai output/performance, not complete application regression coverage.
- Qwen3.8 9B answered 4 on a fresh launch; after repeated switches it was safely refused
  with insufficient VRAM. Investigate retained buffers/allocation lifetime across unloads.
- Granite returned generation failed with zero prompt tokens. Its KV estimate was zero;
  inspect the adapter's zero-KV rejection for recurrent models before judging compatibility.
- BoHF was refused after Granite with only about 306 MiB free. It has older successful
  console evidence; retry cold and then verify switching.
- ACE-Step previously generated one valid 8-second WAV; repeat generation failed allocating
  the VAE workspace. LM KV caching does not fix VAE allocation. Investigate VAE chunking
  and buffer lifetime in upstream audio.cpp.
- Mistral and MiniCPM passed several short GPU checks, but a cumulative switch crash remains.
- Run the media matrix for SD-Turbo, Realistic Vision, Stable Audio, ACE-Step, Kokoro and
  Chatterbox before claiming that every catalog model works. The smoke reports include
  failures; do not treat all JSON files as passing evidence.

## External blocker

The last checks returned No route to host/timeouts on payload, FTP and control services.
The persistent goal is blocked awaiting console connectivity. No reboot was performed.
The last confirmed running title was PPSA99023, PID 187. A control command expects
`agent kill <TITLE-ID>`, not a PID; its attempted title kill was not confirmed. Recheck
process/mount state before updating files. All source, benchmark reports and small generated
media artifacts are saved in this handoff branch. No production title changes are authorized.
