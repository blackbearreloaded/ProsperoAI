#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds RADV (Mesa's Vulkan driver) for the PS5 from Mihawk's pinned checkouts, with the same
# recipe ProsperoEden uses, and writes libvulkan_radeon.ps5.a to the release folder. Run after
# `make deps`. The host tools the build needs are listed in docs/BUILDING.md.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
refs="$root/.deps"
vulkan="$refs/mihawk-vulkan-review"
mesa="$refs/mihawk-mesa-review"
sdk_fork="$refs/mihawk-sdk-review"
release="$vulkan/.deps/native/radv-release"
local_tools="$HOME/.local"

pin() {
    python3 -c 'import json, sys; print(next(i["commit"] for i in json.load(open(sys.argv[1]))["items"] if i["name"] == sys.argv[2]))' \
        "$root/tools/deps.json" "$1"
}
for check in "ps5-vulkan-tools $vulkan" "ps5-mesa $mesa" "ps5-payload-sdk-fork $sdk_fork"; do
    read -r name path <<< "$check"
    [[ -d $path/.git ]] || { echo "missing $path: run 'make deps'" >&2; exit 2; }
    [[ $(git -C "$path" rev-parse HEAD) == "$(pin "$name")" ]] ||
        { echo "$path is not at the $name revision pinned in tools/deps.json" >&2; exit 2; }
done

meson="${MESON:-$HOME/.venvs/meson/bin/meson}"
ninja_bin="${NINJA:-$HOME/.venvs/meson/bin/ninja}"
[[ -x $meson ]] || { echo "meson 1.4+ needed: python3 -m venv ~/.venvs/meson && ~/.venvs/meson/bin/pip install 'meson>=1.4' ninja" >&2; exit 2; }
[[ -x $ninja_bin ]] || { echo "ninja not found" >&2; exit 2; }

# Mihawk's recipe finds glslangValidator on PATH and parses its --version. The 12.2 validator
# (built from the glslang 12.2.0 tag) is put first, with a wrapper that prints the first
# --version line only, which is the form Meson reads.
glslang_bin="$local_tools/glslang-12.2/bin"
[[ -x $glslang_bin/glslangValidator ]] ||
    { echo "glslang 12.2 validator missing: see docs/BUILDING.md (Vulkan backend section)" >&2; exit 2; }

export PS5_MESA_FORK="$mesa"
export PS5_PAYLOAD_SDK_FORK="$sdk_fork"
export MESON="$meson"
export NINJA="$ninja_bin"
export BUILD_JOBS="${BUILD_JOBS:-$(nproc)}"
export CMAKE_BUILD_PARALLEL_LEVEL="$BUILD_JOBS"
export PATH="$glslang_bin:$(dirname "$meson"):$PATH"
export PKG_CONFIG_PATH="$local_tools/glslang-12.2/lib/pkgconfig:$local_tools/spirv-tools-2024/lib/pkgconfig:$local_tools/spirv-llvm-19/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CMAKE_PREFIX_PATH="$local_tools/clang-19:$local_tools/glslang-12.2:$local_tools/spirv-tools-2024:$local_tools/spirv-llvm-19${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

bash "$vulkan/tools/setup-native-dependencies.sh"
bash "$vulkan/tools/build-radv.sh" release

[[ -s $release/lib/libvulkan_radeon.ps5.a ]] || { echo "RADV archive not produced" >&2; exit 1; }
echo "RADV for PS5: $release/lib/libvulkan_radeon.ps5.a"
