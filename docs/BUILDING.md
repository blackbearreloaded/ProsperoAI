# Building ProsperoAI

ProsperoAI builds on Linux (tested on Debian 12; Ubuntu and WSL should work). One command
fetches the pinned inputs and builds the app folder:

```bash
make deps        # fetch missing dependencies at their pinned revisions
make app         # build dist/PPSA99004 (eboot.bin + sce_sys + models/)
```

> **Work in progress.** This branch is a migration in progress. The GPU backend and the
> network layer are being changed, and the current build is **not verified to boot on a
> console**. See [Status](#status).

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
export PS5_HOST=10.0.0.127 FTP_PORT=2120 DEPLOY_FORMAT=ffpfsc USE_CCACHE=0
bash tools/deploy.sh
```

With `DEPLOY_FORMAT=ffpfsc` the app is installed as one package image. A folder deploy over an
already-registered title can leave the console's title database inconsistent (`CE-107750-0`);
undeploy first if that happens.

## Status

What works and what does not, as of this branch:

- **Works (host):** `make deps`, `make deps-status`, `make app` (links, signs `eboot.bin`), `make ffpfsc`.
- **Verified on console (earlier builds):** the original app, and the package-image install path.
- **Not verified on console:** the network layer (`src/http_server.cpp` now calls `sceNet*`
  directly through the SDK import stubs instead of resolving them with `sceKernelDlsym`).
- **Known problems:** the modified runtime shim (`runtime/libc.prx`, `tooling/native/`) has not
  booted on the console. Adding libSceNet to the shim's NEEDED list was the first suspect; it is
  kept here because the migration may remove the shim dependency entirely.
- **Known crash:** `scePthreadDetach()` crashes the title on this runtime; the HTTP server does not
  detach its threads for that reason.
- **Planned:** replace the AGC inference backend with llama.cpp's Vulkan backend running on the
  RADV driver from the Mihawk Mesa checkout. Nothing of that is wired into the build yet.
