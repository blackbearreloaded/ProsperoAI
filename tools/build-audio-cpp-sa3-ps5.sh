#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# Compatibility entry point. audio.cpp is configured as a subproject of the
# shared runtime so it links the same upstream GGML target as text, SD and Kokoro.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$root/tools/deps.py" fetch stable-diffusion-vulkan llama-cpp llama-prism ggml-upstream kokoro-cpp audio-cpp espeak-ng-headers
python3 "$root/tools/prepare-audio-cpp-sa3.py" \
    --source "$root/build/audio-cpp-upstream" \
    --ggml-source "$root/build/ggml-upstream"
cmake --build "$root/build/vulkan-runtime-upstream-ps5" \
    --target audiocpp --parallel "${BUILD_JOBS:-4}"
