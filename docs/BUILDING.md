# Building ProsperoAI

ProsperoAI builds on Linux (tested on Debian 12; Ubuntu and WSL should work). One command
fetches the pinned inputs and builds the app folder:

```bash
make deps        # fetch missing dependencies at their pinned revisions
make app         # build dist/PPSA99004 and dist/PPSA99004.zip
```

> **Work in progress.** The Vulkan folder build and HTTP generation are verified on PS5 FW 12.70. See [benchmark results](VK_BENCHMARK_2026-10-08.md) and [current status](VK_ACTIVE.md).

## Make targets

| Target | What it does |
|---|---|
| `make` / `make app` | Build `dist/PPSA99004` and a ZIP of that app folder |
| `make deps` | Fetch missing dependencies (SDK, zlib, pinned git inputs from `tools/deps.json`) |
| `make deps-status` | List each pinned input, where it lives and whether it matches its pin |
| `make doctor` | Check the host tools |
| `make libc` | Rebuild and verify `runtime/libc.prx` (the clean-room runtime shim) |
| `make deploy PS5_HOST=<address>` | Build and FTP-deploy to `/data/homebrew/PPSA99004` |
| `make undeploy PS5_HOST=<address>` | Remove this title from `/data/homebrew` |
| `make test` | Host integration tests of the tooling |
| `make lint` | Format, static-analysis and metadata checks |
| `make clean` / `make distclean` | Remove `build/` and `dist/` (`distclean` also removes `.deps/`) |

Useful variables: `USE_CCACHE=0` (ccache is optional), `FTP_PORT` (default `2121`), and
`DEPLOY_DRY_RUN=1` (print the folder-deploy plan without sending it).

## Dependencies

Every input is pinned in `tools/deps.json` and fetched by `make deps` (`tools/deps.py`) only when
it is missing. Archives are checked against their SHA-256 before use; git repositories are fetched
at their pinned commit. Nothing that already exists is modified.

Inside this repository, in `.deps/`:

- **PS5 Payload SDK v0.42** (archive, SHA-256 pinned), with `prospero-clang` and the import stubs.
- **llama.cpp** at the pinned commit (the Vulkan backend migration is developed against it).
- **Khronos Vulkan-Headers** at the pinned commit (newer than most distributions ship; ggml-vulkan
  needs `VK_EXT_layer_settings`).

Inside this repository (`.deps/`), as git checkouts:

- **Mihawk's PS5_Vulkan, PS5_Mesa and PS5_PayloadSDK** (`.deps/mihawk-*-review`): the RADV driver and
  its build recipe. The same pins are used by ProsperoEden.

The clean-room runtime shim (`runtime/libc.prx`) is generated from `tooling/native/` by `make libc`.

## Host tools

`make doctor` lists what is missing. For the current build you need `clang-18`, `lld-18`, `cmake`,
`ninja`, `make`, `python3` (3.11+), `curl`, `unzip`, `sha256sum`. For the Vulkan backend work you
also need `glslc` (shaderc), `spirv-headers`, and `glslangValidator`. `ccache` is optional.

Ubuntu/Debian example:

```bash
sudo apt-get install -y clang-18 lld-18 cmake ninja-build ccache glslc spirv-headers glslang-tools python3-mako
```

## Vulkan backend (work in progress)

These steps need no console:

```bash
make deps             # pinned llama.cpp and Vulkan-Headers next to the SDK
make llama-vulkan     # host llama-cli with the Vulkan backend, and a PS5 syntax check of ggml-vulkan
make radv             # RADV for the PS5 (see below for the host tools)
```

`tools/build-llama-vulkan.sh` writes its build to `build/llama-vulkan/`.

Building RADV for the PS5 (`make radv`) uses Mihawk's pinned recipe in `.deps/mihawk-vulkan-review`
and needs these host tools, all built into `~/.local` (no system packages are changed beyond the
apt packages listed):

```bash
sudo apt-get install -y llvm-19-dev libclc-19-dev libclang-19-dev libclang-cpp19-dev clang-19 \
    libllvmspirvlib-19-dev ninja-build ccache
python3 -m venv ~/.venvs/meson && ~/.venvs/meson/bin/pip install 'meson>=1.4' ninja
```

Three components are built from source because Debian 12 does not ship them at the versions RADV
requires (LLVMSPIRVLib 19.1, SPIRV-Tools 2024.1+, glslang 12.2+):

- `SPIRV-LLVM-Translator` branch `llvm_release_190` into `~/.local/spirv-llvm-19`
- `SPIRV-Tools` tag `v2024.4` (with `python3 utils/git-sync-deps`) into `~/.local/spirv-tools-2024`
- `glslang` tag `12.2.0` (with `python3 update_glslang_sources.py`, then `-DENABLE_GLSLANG_BINARIES=ON`)
  into `~/.local/glslang-12.2`. Meson reads the first line of `glslangValidator --version`, so
  `~/.local/glslang-12.2/bin/glslangValidator` is a small wrapper that prints only that line.

The clang static libraries (`clangBasic`, `clangAST`, ...) are built from the LLVM 19.1.7 sources
(`clang` with `-DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm` and tests off) into `~/.local/clang-19`.

`make radv` checks the pinned Mihawk checkouts, sets the paths above and runs Mihawk's
`build-radv.sh release`. The output is
`.deps/mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`.

## Deploying to a console

The console needs a homebrew FTP server listening on `FTP_PORT` (the tested setup uses port 2120).

```bash
export PS5_HOST=<PS5-IP> FTP_PORT=2120 USE_CCACHE=0
bash tools/deploy.sh
```

Development deployment uploads the app folder. Use a separate test title for console work and
stop it before updating files. Registration is separate from the files in `/data/homebrew`;
deleting files alone does not remove a registered title.

## Status

The Vulkan folder title and network API have been verified on PS5 FW 12.70. Mistral 7B Q4_0 reaches a median 69.72 decode tokens/s versus 47.96 for the official upstream AGC release. The optimized isolated loader prepares a raw folder GGUF in a median 2.84 seconds across three fresh process launches; AGC was rechecked at 1.53 seconds. Compressed ffpfsc model reads remain much slower. See [loading measurements](VK_LOAD_BENCHMARK_2026-10-09.md). See [current status](VK_ACTIVE.md) and the [benchmark report](VK_BENCHMARK_2026-10-08.md) for protocol and limitations.

## PS5 llama.cpp Vulkan test build

The WIP console build uses the reviewed RADV archive and SDK under `.deps/mihawk-vulkan-review/`. Build the host tools and host shader generator first, then:

```bash
make llama-ps5           # static llama.cpp CPU/Vulkan libraries
make vulkan-smoke       # instance and physical-device discovery
make llama-vulkan-title # tiny GGUF Vulkan/CPU token validation
make app-vulkan         # complete UI/HTTP app in test slot PPSA99023
```

Model tests expect `build/vulkan-models/stories260K.gguf`. Set `MODEL_GGUF=/absolute/path/model.gguf` to package another model. Production Vulkan and AGC runtimes use the shared `/data/homebrew/prosperoai/models` directory: Vulkan discovers GGUF files and AGC discovers prepared model folders. The isolated loading benchmark still packages its fixture inside `/app0/models`. The Vulkan Models screen can browse public Hugging Face repositories and download verified GGUF files to the shared directory. Downloads are limited to 7 GiB to leave room for Vulkan weights and context memory; that limit does not guarantee every model/context combination fits. The test title runs inside a filesystem sandbox, so its shared directory is exposed by the narrowly scoped nullfs mount payload. Build it with `bash tools/build-model-mount.sh` and load it once after each console boot; it mounts only the configured ProsperoAI test title while that title is running. Its default target is PPSA99023. Outputs are in `build/prospero-vulkan/`, `build/llama-vulkan-title/`, or `build/vulkan-smoke/`. These targets do not deploy. Never overwrite PPSA99004 for testing.

Build jobs default to available CPUs (four on this host). Compiler cache defaults to `build/ccache`, including host tools. Incremental PS5 llama library build measured about 1.6 seconds with no changes; model copies and multi-gigabyte image compression still take time. Host iGPU is not used for compilation.

For development, `make app-vulkan-folder` skips image compression. Upload the folder with `python3 tools/ps5ctl.py ftp put-dir build/prospero-vulkan/PPSA99023 /data/homebrew/PPSA99023`, then rescan with ShadowMount. The FTP helper sets executable permissions on eboot.bin and PRX modules. An earlier folder launch refusal (0x80aa001a, errno 13) was fixed by SITE CHMOD 755 on these files. Once registered and stopped, update only changed files; immutable model files need not be uploaded again. Keep the same title metadata and do not replace an image while its mount is still live.

## OpenAI API and OpenCode

To build the isolated model-loading benchmark in the existing test slot `PPSA99019`:

```bash
make llama-ps5
LLAMA_SMOKE=1 LOAD_BENCHMARK=1 MODEL_GGUF=/absolute/path/model.gguf bash tools/build-vulkan-smoke.sh
```

It measures three alternating serial/parallel pairs, separating weight loading, context
creation and first evaluation, and compares the first greedy token. The Vulkan device
and process are shared across rounds; these are repeated loads, not independent cold
launches. The GGUF must be packaged in `/app0/models` because the title sandbox cannot
read another title's model directory. Read the report through FTP at
`/mnt/sandbox/PPSA99019_000/download0/prospero-load-benchmark.txt` while the test is running.
Installation and repeated launches within a console-testing task are covered by the authorization rules in `AGENTS.md`.

The Vulkan build also exposes `/v1/models` and `/v1/chat/completions`, including SSE and portable function calls. See [OpenCode configuration](OPENCODE.md) for bearer keys, context sizing and protocol limits. The native Vulkan title links the existing AGC media backend alongside Vulkan text inference. Voice generation and switching back to Vulkan text have been verified on console; Image and Audio generation in this combined build have not yet been exercised.

Default Vulkan backend logging forwards WARN/ERROR. Create `/app0/vulkan_verbose_logging.txt` only for detailed diagnosis; synchronous full debug output materially slows loading. For first-load-only benchmark runs, add `/app0/load_parallel_only.txt` (three optimized loads per launch).

## Native UI Vulkan presentation probe

`UI_PROBE=1 bash tools/build-vulkan-smoke.sh` builds an independent display probe
in PPSA99019. This verifies graphics presentation without OpenGL or a model;
upstream UI design remains unchanged. See [migration stages](VK_UI_MIGRATION.md)
and [current evidence](VK_ACTIVE.md).

### Download presets

The native Models library includes pinned Text (Mistral 7B Q4_0, Qwen3.5 9B Q4_0),
Image (SD-Turbo FP16), Audio (Stable Audio Open Small FP16), and Voice (Kokoro 82M FP16)
presets. Cross downloads the selected preset; L2/R2 change category and arrows select
cards. The progress bar shows bytes and percentage for the entire bundle. Search typing
uses Triangle for Space and R2 for Done.

Text presets download standalone GGUF files for Vulkan. Media presets download complete
prepared folders, including their licenses, for the AGC media backend in the same native
Vulkan title. Only one inference backend owns model allocations at a time. The hybrid
build isolates the older SD ggml symbols from the newer Vulkan llama.cpp ABI. Every
file is pinned to a repository commit and checked against its size and SHA-256. Media
bundles are staged in hidden directories until all files pass verification. Failed
staging files are overwritten on retry and never appear as usable model bundles.

`model-tools/presets.json` records the pinned media manifests; Text uses the source
entries in the existing conversion recipes. Run `python3 tools/generate-model-presets.py`
after changing these records to regenerate `include/model_presets.hpp` without network
access. Refreshing manifests requires fetching every repository tree page and computing
SHA-256 for small files whose Hub entry has no LFS hash. Never use a truncated tree as
a complete bundle.

A shared model directory exposed by nullfs may be readable/writable even when its parent
directories reject sandboxed `mkdir` or `access`. The downloader first checks the mounted
leaf with `stat`. After a console reboot reload the scoped model-mount payload described
above. Storage errors include errno and distinguish missing storage access from disk
write failures. Hugging Face CDN redirects are followed explicitly because automatic
redirects on this firmware returned HTTP 200 with an empty file body.

The HTTP UI exposes the same presets through `/api/models/presets` and shows download
progress from `/api/models/download`. Workspace category buttons start a conversation
with an installed model of that type, or open its Models category when none is installed.
Back to Workspace preserves browser history; each history row has a confirmed delete action.
