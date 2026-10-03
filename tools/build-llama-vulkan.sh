#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host build of llama.cpp with the Vulkan backend, plus a PS5 syntax check of
# ggml-vulkan.cpp with the Payload SDK clang. Uses the pinned checkouts from
# `make deps` (tools/deps.json). Nothing is installed; missing host tools are
# reported with the apt packages that provide them.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
llama="$root/.deps/llama.cpp"
headers="$root/.deps/Vulkan-Headers/include"
sdk="$root/.deps/native/ps5-payload-sdk"
build="$root/build/llama-vulkan"
spirv_shim="$build/include-shim"

for command in cmake ninja glslc; do
    command -v "$command" >/dev/null || {
        echo "missing: $command (apt: cmake ninja-build glslc)" >&2
        exit 2
    }
done
[[ -d $llama ]] || { echo "missing $llama: run 'make deps' first" >&2; exit 2; }
[[ -d $headers ]] || { echo "missing $headers: run 'make deps' first" >&2; exit 2; }
[[ -x $sdk/bin/prospero-clang ]] || { echo "missing PS5 SDK: run 'make deps' first" >&2; exit 2; }

cmake -S "$llama" -B "$build/host" -G Ninja \
    -DGGML_VULKAN=ON \
    -DVulkan_INCLUDE_DIR="$headers" \
    -DLLAMA_CURL=OFF \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build/host" --target llama-cli

# ggml-vulkan includes <spirv/unified1/spirv.hpp> from the SPIRV-Headers package.
# Expose only that folder to the PS5 compile, so the host glibc headers stay out.
mkdir -p "$spirv_shim/spirv"
spirv_hpp=$(find /usr/include/spirv -maxdepth 2 -name spirv.hpp -path '*unified1*' 2>/dev/null | head -1)
[[ -n $spirv_hpp ]] || { echo "missing SPIRV-Headers: apt install spirv-headers" >&2; exit 2; }
ln -sfn "$(dirname "$spirv_hpp")" "$spirv_shim/spirv/unified1"

generated=$(find "$build/host" -name ggml-vulkan-shaders.hpp -print -quit)
[[ -n $generated ]] || { echo "generated ggml-vulkan-shaders.hpp not found" >&2; exit 2; }

PS5_PAYLOAD_SDK="$sdk" PS5_CLANG="$sdk/bin/prospero-clang++" USE_CCACHE=0 \
    sh "$root/tooling/prospero-clang18" -std=c++17 -fsyntax-only \
    -I"$llama/ggml/include" -I"$llama/ggml/src" -I"$llama/ggml/src/ggml-vulkan" \
    -I"$headers" -I"$spirv_shim" -I"$(dirname "$generated")" \
    -DGGML_USE_VULKAN "$llama/ggml/src/ggml-vulkan/ggml-vulkan.cpp"

echo "llama-vulkan: host build ok ($build/host/bin/llama-cli); PS5 syntax check ok"
