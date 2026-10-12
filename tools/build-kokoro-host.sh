#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Host build of kokoro.cpp (ggml speech runtime) against the same pinned ggml the PS5 app
# uses, so numerical and per-stage behaviour can be checked before touching the PS5 link.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$root/tools/deps.py" fetch kokoro-cpp llama-prism
source_tree="$root/build/prism-llama-source"
export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
cmake -S "$root/.deps/kokoro.cpp" -B "$root/build/kokoro-host" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKOKORO_VULKAN=ON -DKOKORO_BUILD_CLI=ON -DKOKORO_BUILD_TESTS=ON \
    -DKOKORO_GGML_DIR="$source_tree/ggml" \
    -DVulkan_INCLUDE_DIR="$root/.deps/Vulkan-Headers/include"
cmake --build "$root/build/kokoro-host" --parallel "${BUILD_JOBS:-4}"
