# Building ProsperoAI

ProsperoAI builds on Linux (tested on Debian 12; Ubuntu and WSL should work). One command
fetches the pinned inputs and builds the app folder:

```bash
make deps        # fetch missing dependencies at their pinned revisions
make app         # build dist/PPSA99004 (eboot.bin + sce_sys + models/)
```

> **Work in progress.** The Vulkan folder build and HTTP generation are verified on PS5 FW 12.70. See [benchmark results](VK_BENCHMARK_2026-10-08.md) and [current status](VK_ACTIVE.md).

## Make targets

| Target | What it does |
|---|---|
| `make` / `make app` | Build `dist/PPSA99004` |
| `make deps` | Fetch missing dependencies (SDK, zlib, pinned git inputs from `tools/deps.json`) |
| `make deps-status` | List each pinned input, where it lives and whether it matches its pin |
| `make doctor` | Check the host tools |
| `make libc` | Rebuild and verify `runtime/libc.prx` (the clean-room runtime shim) |
| `make ffpfsc` | Build the compressed `dist/PPSA99004.ffpfsc` package image |
| `make deploy PS5_HOST=<address>` | Build and FTP-deploy to `/data/homebrew/PPSA99004` |
| `make undeploy PS5_HOST=<address>` | Remove this title from `/data/homebrew` |
| `make test` | Host integration tests of the tooling |
| `make lint` | Format, static-analysis and metadata checks |
| `make clean` / `make distclean` | Remove `build/` and `dist/` (`distclean` also removes `.deps/`) |

Useful variables: `USE_CCACHE=0` (ccache is optional), `DEPLOY_FORMAT=folder|ffpfsc`,
`FTP_PORT` (default `2121`), `DEPLOY_DRY_RUN=1` (print the deploy plan without sending it).

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
export PS5_HOST=<PS5-IP> FTP_PORT=2120 DEPLOY_FORMAT=ffpfsc USE_CCACHE=0
bash tools/deploy.sh
```

With `DEPLOY_FORMAT=ffpfsc` the app is installed as one package image. A folder deploy over an
already-registered title can leave the console's title database inconsistent (`CE-107750-0`);
undeploy first if that happens.

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

Model tests expect `build/vulkan-models/stories260K.gguf`. Set `MODEL_GGUF=/absolute/path/model.gguf` to package another model. The Vulkan app discovers raw GGUF files under `/app0/models` and `/data/homebrew/prosperoai/models`; its Models screen can browse a public Hugging Face repository and download a verified GGUF to the latter directory. Downloads are limited to 7 GiB to leave room for Vulkan weights and context memory; that limit does not guarantee every model/context combination fits. The original AGC backend still uses prepared model folders. Outputs are in `build/prospero-vulkan/`, `build/llama-vulkan-title/`, or `build/vulkan-smoke/`. These targets do not deploy. Console testing is authorized by the user’s task request as described in `AGENTS.md`; remove stale test registration before replacing a registered image. Never overwrite PPSA99004 for testing.

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

The Vulkan build also exposes `/v1/models` and `/v1/chat/completions`, including SSE and portable function calls. See [OpenCode configuration](OPENCODE.md) for bearer keys, context sizing and protocol limits. Image, audio and speech remain on the existing AGC build; their Vulkan migration is planned.

Default Vulkan backend logging forwards WARN/ERROR. Create `/app0/vulkan_verbose_logging.txt` only for detailed diagnosis; synchronous full debug output materially slows loading. For first-load-only benchmark runs, add `/app0/load_parallel_only.txt` (three optimized loads per launch).
