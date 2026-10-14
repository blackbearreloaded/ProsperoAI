#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source_tree="$root/build/onnxruntime-upstream"
build_tree="$root/build/onnxruntime-ps5"
sdk="$root/.deps/native/ps5-payload-sdk"
commit=89746dc19a0a1ae59ebf4b16df9acab8f99f3925
patch="$root/tools/patches/onnxruntime-1.22.1-ps5.patch"
python3 "$root/tools/deps.py" fetch onnxruntime
actual=$(git -C "$source_tree" rev-parse HEAD)
[[ $actual == "$commit" ]] || {
  echo "ONNX Runtime source is $actual, expected pinned commit $commit" >&2
  exit 2
}
if git -C "$source_tree" apply --reverse --check "$patch" >/dev/null 2>&1; then
  : # The patch is already present in this working tree.
elif git -C "$source_tree" apply --check "$patch"; then
  git -C "$source_tree" apply "$patch"
else
  echo "ONNX Runtime PS5 patch does not apply cleanly" >&2
  exit 2
fi
export TMPDIR="$root/build/compiler-tmp"
mkdir -p "$TMPDIR"
python3 "$source_tree/tools/ci_build/build.py" \
  --build_dir "$build_tree" \
  --config Release \
  --update \
  --build \
  --build_shared_lib \
  --parallel "${BUILD_JOBS:-4}" \
  --skip_submodule_sync \
  --cmake_extra_defines \
    "CMAKE_TOOLCHAIN_FILE=$sdk/toolchain/prospero.cmake" \
    "onnxruntime_BUILD_SHARED_LIB=ON" \
    "BUILD_SHARED_LIBS=OFF" \
    "onnxruntime_BUILD_UNIT_TESTS=OFF" \
    "onnxruntime_ENABLE_CPUINFO=OFF" \
    "onnxruntime_USE_OPENMP=OFF" \
    "onnxruntime_USE_VCPKG=OFF" \
    "onnxruntime_MINIMAL_BUILD=OFF" \
    "onnxruntime_BUILD_CSHARP=OFF" \
    "onnxruntime_BUILD_NODEJS=OFF" \
    "onnxruntime_BUILD_JAVA=OFF" \
    "onnxruntime_BUILD_OBJC=OFF" \
    "onnxruntime_ENABLE_PYTHON=OFF" \
    "onnxruntime_USE_MIMALLOC=OFF" \
    "onnxruntime_USE_FULL_PROTOBUF=OFF"
[[ -f "$build_tree/Release/libonnxruntime.so.1.22.1" ]] || {
  echo "ONNX Runtime build completed without the expected shared library" >&2
  exit 1
}
python3 "$root/tools/package-onnxruntime-ps5.py" \
  "$build_tree/Release/CMakeFiles/onnxruntime.dir/link.txt" \
  "$build_tree/Release/libonnxruntime-ps5.o"
echo "ONNX Runtime PS5 static link object: $build_tree/Release/libonnxruntime-ps5.o"
