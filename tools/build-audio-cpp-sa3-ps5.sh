#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source="$root/build/audio-cpp-upstream"
ggml="$root/build/prism-llama-source/ggml"
build="$root/build/audio-cpp-sa3-ps5-repro"
sdk="$root/.deps/mihawk-vulkan-review/.deps/native/ps5-payload-sdk"
headers="$root/.deps/src/vulkan-headers-spike/include"
radv="$root/.deps/mihawk-vulkan-review/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
jobs=${BUILD_JOBS:-$(nproc)}

for required in "$source/CMakeLists.txt" "$ggml/CMakeLists.txt" \
    "$sdk/bin/prospero-lld" "$headers/vulkan/vulkan.hpp" "$radv"; do
    [[ -e $required ]] || { echo "missing SA3 PS5 build dependency: $required" >&2; exit 2; }
done

python3 "$root/tools/prepare-audio-cpp-sa3.py" --source "$source"
python3 "$root/tools/prepare-ggml-graph-setter.py" --source "$ggml"
export PATH="$sdk/bin:$PATH"

cmake -S "$source" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/tools/audio-cpp-ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DAUDIOCPP_BUILD_C_API=ON -DAUDIOCPP_MODEL_SET=custom -DAUDIOCPP_MODELS=stable_audio,ace_step \
    -DAUDIOCPP_GGML_SOURCE_DIR="$ggml" \
    -DENGINE_ENABLE_VULKAN=ON -DENGINE_ENABLE_OPENMP=OFF \
    -DSPM_NO_THREADLOCAL=ON -DGGML_VULKAN=ON -DGGML_OPENMP=OFF -DGGML_NATIVE=OFF \
    -DVulkan_INCLUDE_DIR="$headers" -DVulkan_LIBRARY="$radv" \
    -DSPIRV-Headers_DIR=/usr/share/cmake/SPIRV-Headers
cmake --build "$build" --target audiocpp --parallel "$jobs"
