#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-ps5}
[[ $mode == ps5 || $mode == host ]] || { echo "usage: $0 [ps5|host]" >&2; exit 2; }
python3 "$root/tools/deps.py" fetch stable-diffusion-vulkan llama-prism
source_tree="$root/build/prism-llama-source"
export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
export TMPDIR="$root/build/compiler-tmp"
mkdir -p "$TMPDIR"
options=(-DPROS_LLAMA_SOURCE="$source_tree" -DPROS_PRISM_RUNTIME=ON -DVulkan_INCLUDE_DIR="$root/.deps/Vulkan-Headers/include" -DGGML_CCACHE=ON)
if [[ $mode == ps5 ]]; then
    python3 "$root/tools/prepare-llama-ps5-io.py" --source "$source_tree"
    python3 "$root/tools/prepare-llama-ps5-vulkan.py" "$source_tree"
    mkdir -p "$root/build/llama-vulkan/include-shim/spirv"
    ln -sfn /usr/include/spirv/unified1 "$root/build/llama-vulkan/include-shim/spirv/unified1"
    options+=(-DCMAKE_TOOLCHAIN_FILE="$root/tools/ps5-llama-toolchain.cmake"
        -DVulkan_LIBRARY="$root/.deps/mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
        -DGGML_AVX512=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON
        -DSPIRV-Headers_DIR=/usr/share/cmake/SPIRV-Headers -U CMAKE_CXX_FLAGS)
fi
cmake -S "$root/tools/vulkan-runtime" -B "$root/build/vulkan-runtime-$mode" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "${options[@]}"
targets=(llama stable-diffusion)
[[ $mode != host ]] || targets+=(vulkan-runtime-abi-test vulkan-runtime-media-test)
cmake --build "$root/build/vulkan-runtime-$mode" --target "${targets[@]}" \
    --parallel "${BUILD_JOBS:-4}"
