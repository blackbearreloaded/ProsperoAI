#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ref="$root/.deps/mihawk-vulkan-review"
sdk="$ref/.deps/native/ps5-payload-sdk"
build="$root/build/audio-cpp-sa3-ps5-repro"
source="$root/build/audio-cpp-upstream"
output="$root/build/audio-cpp-sa3-ps5-link-smoke"
smoke_stage=${SA3_SMOKE_STAGE:-4}

if [[ ! $smoke_stage =~ ^[1-4]$ ]]; then
    echo "SA3_SMOKE_STAGE must be 1 (registry), 2 (model), 3 (Vulkan session), or 4 (generation)" >&2
    exit 2
fi

for required in "$build/bin/libaudiocpp.a" "$build/libengine_runtime.a" \
    "$build/external/sentencepiece/src/libsentencepiece.a" "$build/libcjson_vendor.a" \
    "$build/libyaml_vendor.a" "$build/ggml/src/libggml.a" \
    "$build/ggml/src/libggml-cpu.a" "$build/ggml/src/libggml-base.a" \
    "$build/ggml/src/ggml-vulkan/libggml-vulkan.a"; do
    [[ -f $required ]] || { echo "missing SA3 PS5 archive: $required" >&2; exit 2; }
done

mkdir -p "$output"
export PATH="$sdk/bin:$PATH"
export PS5_CLANG=${PS5_CLANG:-clang-18}
cc() { PS5_PAYLOAD_SDK="$sdk" USE_CCACHE=0 sh "$root/tooling/prospero-clang18" "$@"; }
cc -std=c11 -O2 -DSA3_SMOKE_STAGE="$smoke_stage" -I "$source/include" -c \
    "$root/tests/audio_cpp_ps5_link_smoke.c" -o "$output/smoke.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c \
    "$ref/tooling/native/app_crt.cpp" -o "$output/app_crt.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c \
    "$ref/tooling/native/app_cpp_runtime.cpp" -o "$output/app_cpp_runtime.o"
# These tiny objects describe the AGC modules imported by RADV's PS5 winsys to
# the ELF converter. They are not linked as an AGC inference backend; boot still
# requires the real platform modules, as documented in docs/VK_PLAN.md.
cc -std=c11 -O2 -fPIC -c "$ref/vendor/ps5/sdk/stubs/agc_canary_link_stub.c" \
    -o "$output/Agc.o"
cc -std=c11 -O2 -fPIC -c "$ref/vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c" \
    -o "$output/AgcDriver.o"
cc -std=c11 -O2 -c "$root/tooling/native/llama_ps5_compat.c" \
    -o "$output/llama_compat.o"
"$sdk/bin/prospero-lld" --shared -soname libSceAgc.prx -o "$output/libSceAgc.so" \
    "$output/Agc.o"
"$sdk/bin/prospero-lld" --shared -soname libSceAgcDriver.prx \
    -o "$output/libSceAgcDriver.so" "$output/AgcDriver.o"

source "$ref/tools/radv-link.sh"
radv_link_recipe "$ref" "$sdk" \
    "$ref/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
for symbol in vkGetInstanceProcAddr vkGetDeviceProcAddr vkCmdCopyBuffer \
    vkGetPhysicalDeviceFeatures2; do
    case "$symbol" in
        vkGetInstanceProcAddr) target=radv_GetInstanceProcAddr ;;
        vkGetDeviceProcAddr) target=vk_common_GetDeviceProcAddr ;;
        vkCmdCopyBuffer) target=vk_common_CmdCopyBuffer ;;
        vkGetPhysicalDeviceFeatures2) target=vk_common_GetPhysicalDeviceFeatures2 ;;
    esac
    radv_link_flags+=("--defsym=$symbol=$target")
done

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr \
    "${radv_link_flags[@]}" --version-script "$ref/tooling/native/app-symbols.map" \
    --exclude-libs=ALL -e _start -o "$output/llvm-pie.elf" \
    "$output/app_crt.o" "$output/app_cpp_runtime.o" "$output/smoke.o" \
    "$output/llama_compat.o" \
    "$output/libSceAgc.so" "$output/libSceAgcDriver.so" \
    --start-group "$build/bin/libaudiocpp.a" "$build/libengine_runtime.a" \
    "$build/external/sentencepiece/src/libsentencepiece.a" "$build/libcjson_vendor.a" \
    "$build/libyaml_vendor.a" "$build/ggml/src/libggml.a" \
    "$build/ggml/src/libggml-cpu.a" "$build/ggml/src/ggml-vulkan/libggml-vulkan.a" \
    "$build/ggml/src/libggml-base.a" --end-group \
    --defsym=vkGetInstanceProcAddr=radv_GetInstanceProcAddr \
    --defsym=vkGetDeviceProcAddr=vk_common_GetDeviceProcAddr \
    --defsym=vkCmdCopyBuffer=vk_common_CmdCopyBuffer \
    --defsym=vkGetPhysicalDeviceFeatures2=vk_common_GetPhysicalDeviceFeatures2 \
    --defsym=execlp=prospero_execlp --defsym=posix_madvise=prospero_posix_madvise \
    "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so

echo "PS5 Stable Audio 3 C API stage $smoke_stage ELF: $output/llvm-pie.elf"
