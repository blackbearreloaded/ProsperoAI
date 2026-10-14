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

## Chatterbox and recording — current result

The Vulkan adapter now reuses an engine session and releases it on model switches.
Pinned audio.cpp preparation bounds graph/weight metadata arenas; S3 and HiFT allocation
failures no longer occur in the latest runs. The voice catalog returns values rather than
thread-local vector pointers, removing the observed HTTP-worker teardown crash.
The original 20–40 ms failure occurs with Cemo, Alana and the valid custom reference.
T3 decode imported only the used KV prefix, leaving the masked tail uninitialized.
Zeroing the whole cache capacity before import changes Cemo to 15.34/12.66-second
non-clipped WAVs and fixes the observed all-invalid decode logits. The custom cold run
still failed, while its repeat generated 15.36 seconds. These short fixtures should not
normally produce so much audio: the user confirmed the Cemo speech is unintelligible; EOS correctness remains unverified.
Do not claim complete Chatterbox quality success. The adapter now rejects outputs under
250 ms. See zero-kv-tail reports and token trace. Official reference reviewed:
https://github.com/resemble-ai/chatterbox/blob/master/src/chatterbox/tts.py and
https://github.com/resemble-ai/chatterbox/blob/master/src/chatterbox/models/t3/t3.py.
No documented setting change alone resolved the failure. An explicit F32 decode attention
variant matching reference math compiled, but is not yet console verified.
A saved console conversation `test-chatterbox-cemo-20261014-kv` contains both Cemo
artifacts; live browser verification confirms two audio players.

Native DualSense capture and web capture/save are implemented. The user confirmed PS5
recording works. The native voice picker has visible glyph explanations and L3 delete
with confirmation for own/imported samples. Browser capture requires HTTPS/localhost;
on LAN HTTP, the web recorder can use the PS5 microphone through the app API.
Physical persistent output storage is `/data/prosperoai/work/{image,audio,voice}`;
`/download0/ProsperoAI/...` remains the sandbox/HTTP alias.

Web runtime control initialization is fixed, including Chatterbox voice switching.
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

## Console state

Connectivity was restored after the user's reboot. FTP/control/klog/shell are available.
The authorized folder-backed slot is PPSA99023. The latest app build label is
`pr27-memory-zero-kv`; main PPSA99004 was not changed and no reboot was requested.
No commits or pushes were made. Recheck process/mount state before subsequent deployment.
