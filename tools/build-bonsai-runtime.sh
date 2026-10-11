#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Prism host/reference library builds; app uses the shared vulkan-runtime, no deployment.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-ps5}
[[ $mode == ps5 || $mode == host || $mode == app ]] || { echo "usage: $0 [ps5|host|app]" >&2; exit 2; }
source_tree="$root/build/prism-llama-source"
python3 "$root/tools/deps.py" fetch llama-prism
export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
if [[ $mode == app ]]; then
    bash "$root/tools/build-vulkan-runtime.sh" ps5
    bash "$root/tools/build-vulkan-native.sh"
    exit
fi
options=(-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DGGML_NATIVE=OFF
    -DGGML_VULKAN=ON -DGGML_OPENMP=OFF -DGGML_CCACHE=ON
    -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_BUILD_APP=OFF -DLLAMA_CURL=OFF -DLLAMA_OPENSSL=OFF
    -DVulkan_INCLUDE_DIR="$root/.deps/Vulkan-Headers/include")
targets=(llama-completion llama-bench)
if [[ $mode == ps5 ]]; then
    python3 "$root/tools/prepare-llama-ps5-io.py" --source "$source_tree"
    options+=(-DCMAKE_TOOLCHAIN_FILE="$root/tools/ps5-llama-toolchain.cmake"
        -DGGML_AVX512=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON
        -DVulkan_LIBRARY="$root/.deps/mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
        -DSPIRV-Headers_DIR=/usr/share/cmake/SPIRV-Headers
        -DLLAMA_BUILD_COMMON=OFF -DLLAMA_BUILD_TOOLS=OFF)
    targets=(llama ggml-vulkan)
fi
cmake -S "$source_tree" -B "$root/build/prism-vulkan-$mode" -G Ninja "${options[@]}"
cmake --build "$root/build/prism-vulkan-$mode" --target "${targets[@]}" --parallel "${BUILD_JOBS:-4}"
