#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export CMAKE_BUILD_PARALLEL_LEVEL=${BUILD_JOBS:-$(nproc)}
export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
cache=OFF
command -v ccache >/dev/null && cache=ON
mkdir -p "$root/build/llama-vulkan/include-shim/spirv"
ln -sfn /usr/include/spirv/unified1 "$root/build/llama-vulkan/include-shim/spirv/unified1"
cmake -S "$root/.deps/llama.cpp" -B "$root/build/llama-ps5" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/tools/ps5-llama-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF -DGGML_CCACHE="$cache" -DGGML_NATIVE=OFF -DGGML_OPENMP=OFF \
    -DGGML_VULKAN=ON -DGGML_AVX512=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON \
    -DVulkan_INCLUDE_DIR="$root/.deps/Vulkan-Headers/include" \
    -DVulkan_LIBRARY="$root/.deps/mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a" \
    -DSPIRV-Headers_DIR=/usr/share/cmake/SPIRV-Headers \
    -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_TOOLS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF -DLLAMA_BUILD_APP=OFF \
    -DLLAMA_CURL=OFF -DLLAMA_OPENSSL=OFF
cmake --build "$root/build/llama-ps5" --target llama ggml-vulkan --parallel "$CMAKE_BUILD_PARALLEL_LEVEL"
