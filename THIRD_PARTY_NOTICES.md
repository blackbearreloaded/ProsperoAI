# Third-party notices

## Credits and acknowledgements

ProsperoAI builds on the
[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
and the public [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
Its native interface uses [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui)
and [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl).
[SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) (John Törnblom's [PS5 port](https://github.com/ps5-payload-dev/SDL))
remains for media and system helpers. Model runtimes incorporate work from
[llama.cpp](https://github.com/ggml-org/llama.cpp),
[stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp),
[Stable Audio Open Small](https://huggingface.co/stabilityai/stable-audio-open-small),
and [Kokoro](https://huggingface.co/hexgrad/Kokoro-82M).

The shared-Vulkan SD compile probe fetches stable-diffusion.cpp (MIT) at
`f89d9b13d730eabeede7314ce49dacd18d3c90c2`, pinned in `tools/deps.json`.
The experimental shared folder builds this source against llama's ggml; default
release packaging retains the previously shipped media runtime until console gates pass.
Host-only recursive PickleValue adaptation and constant-initialized TLS declarations
are maintained in `tools/vulkan-runtime/`; the pinned source checkout is unchanged.

ProsperoAI is built on the
[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
and uses the public [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
The clean-room application runtime and project-owned source are distributed
under GPL-3.0-or-later.

The interface kit ([ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui),
GPL-3.0-or-later) and the [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) SDK are
build dependencies fetched at pinned versions; neither is kept in this repository. A release
contains, compiled or copied from them:

- the kit's renderer and components, and its "glass" and "paper" interface sounds (the
  author's own, under the project licence);
- Inter, Montserrat and DejaVu Sans Mono, baked from the kit's `third_party/fonts`; their
  licences ship beside the baked fonts in `assets/fonts`;
- [Noto Sans CJK](https://github.com/notofonts/noto-cjk) (Noto Sans SC and KR), Copyright
  2014-2021 Adobe, SIL Open Font License 1.1, fetched at a pinned commit and baked for
  Chinese, Japanese and Korean text; its licence ships as `NotoSansCJK-LICENSE.txt`;
- the ps5-opengl runtime (Mesa and its dependencies); see the SDK's own
  `THIRD_PARTY_NOTICES.md`.

`assets/fonts/legacy-multilingual` is the bitmap font of the first interface, made from
Source Han Sans SC, Noto Sans and DejaVu Sans; their licences are in its `licenses/` folder
and ship with the baked font. `tools/font-baker/bake_list.cpp`, `tools/cjk-ranges.py` and
`tools/fetch-cjk-fonts.sh` come from
[ProsperoTV](https://github.com/blackbearreloaded/ProsperoTV) (GPL-3.0-or-later, same author).

Storage access comes from
[PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon), created by ArkSama,
MIT; the exact-title one-request helper and the cooperative elevation protocol are
[mpereiraesaa's](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon). The build fetches
the helper's source from
[this project's fork](https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon) at the
commit pinned in `tools/build-lapy-helper.py`, builds it for the app's title with the PS5
Payload SDK that upstream requires, checks it against its manifest and packages it as
`lapy.elf` with `licenses/Lapy-MIT.txt`. The client that talks to it
(`vulkan/elevation/`) and `vulkan/net/console_curl.c` come from the PS5 Native App
Boilerplate, GPL-3.0-or-later.

The model downloader links [curl](https://curl.se/) (curl license),
[OpenSSL](https://www.openssl.org/) (Apache-2.0),
[libpsl](https://github.com/rockdaboot/libpsl) (MIT) and
[Zstandard](https://github.com/facebook/zstd) (BSD-3-Clause) from the pinned
[PacBrew](https://github.com/ps5-payload-dev/pacbrew-repo) prefix
(`tools/setup-pacbrew-dependencies.sh`), and the [zlib](https://zlib.net/) (zlib license)
that the Vulkan driver's archive carries.

The update check and the in-place update (`third_party/update-check`,
`third_party/self-update-helper`) come from the PS5 Native App Boilerplate, GPL-3.0-or-later,
as ProsperoLight, ProsperoEden and ProsperoTV use them; what differs is listed in
`third_party/update-check/README.md`. The helper reads release archives with
[miniz](https://github.com/richgel999/miniz) 3.0.2 (`third_party/miniz`, MIT; its licence
ships with the app as `licenses/miniz-MIT.txt`). The check asks the
[homebrew.page](https://homebrew.page) catalog and verifies its Ed25519 signature with
OpenSSL.

The model execution paths incorporate code or static build
artifacts derived from llama.cpp, stable-diffusion.cpp, ggml, espeak-ng,
Kokoro, and the Stable Audio compatibility work in a8nova/adreno-llms. Those
projects retain their respective copyrights and licenses.

No model weights are included in this repository or its automated release.
Curated model repositories record the exact upstream source, preparation
recipe, integrity hashes, and model-specific license:

- [Mistral 7B Instruct v0.3 Q4_0](https://huggingface.co/blackbearreloaded/ProsperoAI-Mistral-7B-Instruct-v0.3-Q4_0-PS5)
- [Qwen3.5 9B Q4_0](https://huggingface.co/blackbearreloaded/ProsperoAI-Qwen3.5-9B-Q4_0-PS5)
- [SD-Turbo FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-SD-Turbo-FP16-PS5)
- [Stable Audio Open Small FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-Stable-Audio-Open-Small-FP16-PS5)
- [Kokoro 82M FP16](https://huggingface.co/blackbearreloaded/ProsperoAI-Kokoro-82M-FP16-PS5)

PlayStation and PS5 are trademarks of Sony Interactive Entertainment.
ProsperoAI is independent homebrew software and is not affiliated with or
endorsed by Sony Interactive Entertainment or the model authors.

## Native tooling

The PS5 ELF converter and FSELF writer in `tooling/native/` are derived from
[SharpProspero](https://github.com/SvenGDK/SharpProspero), Copyright (C) 2026
SvenGDK, GPL-3.0, and were translated to C++ and modified by BlackBearReloaded.

## MiniCPM5 chat template

`tests/fixtures/minicpm5-chat.jinja` is OpenBMB's chat template extracted from
the official MiniCPM5-2B Q8_0 GGUF at revision
`2079a22f3beaa4e306449978533478fe0522f4b3`. It is used as the formatter test oracle.
MiniCPM5 is published under Apache-2.0; see `third_party/minicpm5/LICENSE`.
The optional model preset downloads weights from OpenBMB when requested.

## Experimental Bonsai 2 text runtime

The isolated Bonsai 2 build uses [PrismML-Eng/llama.cpp](https://github.com/PrismML-Eng/llama.cpp)
at `e8fd209c975c80aa7fb36a8423571ff3b1b7e955` (MIT), release
`prism-b10772-e8fd209`. It provides private PTQ1_0/PQ2_0 quantization and Hadamard
kernels. Its verified source archive is pinned in `tools/deps.json`; guarded PS5
positional-read adapters are applied by `tools/prepare-llama-ps5-io.py`.
This is a separately built text experiment, not an unverified replacement of the
shared media ggml ABI. The Prism source licence remains in the dependency tree.
The optional Ternary Bonsai 2 checkpoint is Apache-2.0 and is not bundled.
