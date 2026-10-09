#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ref="$root/.deps/mihawk-vulkan-review"
sdk="$ref/.deps/native/ps5-payload-sdk"
archive="$ref/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
work="$root/build/prospero-vulkan-native"
app="$work/PPSA99023"
kit_stage=$(bash "$root/tools/prepare-ui-kit.sh")
kit=$(bash "$root/tools/prepare-ui-kit.sh" --kit)
fonts=$(bash "$root/tools/bake-fonts.sh")
mkdir -p "$work/obj" "$work/stubs" "$app/sce_sys" "$app/sce_module" "$work/generated"
python3 "$root/tools/prepare-vulkan-ui-shaders.py" --kit "$kit_stage/src" --output "$work/generated"
version=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1]))["contentVersion"])' "$root/sce_sys/param.json")
printf '#define PROSPERO_VERSION "%s"\n#define PROSPERO_BUILD_LABEL "Vulkan"\n' "$version" > "$work/generated/prospero_build.h"
cc(){ PS5_PAYLOAD_SDK="$sdk" sh "$root/tooling/prospero-clang18" "$@"; }
export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
source "$root/tools/ninja-build.sh"
ninja_begin "$work/app.ninja"
objects=()
sources=(src/main.cpp src/native_app.cpp src/native_ui.cpp src/native_ui_screens.cpp src/native_ui_style.cpp
 src/font_set.cpp src/media_preview.cpp src/dev_script.cpp src/gpt_input.cpp src/gpt_ime.cpp
 src/media_output_ps5.cpp src/session_store.cpp src/runtime_support.cpp
 src/ps5_agc_backend.cpp src/ps5_opencl.cpp src/sd_runtime_ps5.cpp src/stable_audio_runtime_ps5.cpp src/agc_lifecycle.cpp
 vulkan/gpt_runtime_hybrid.cpp
 vulkan/gpt_runtime_vulkan.cpp vulkan/http_server.cpp vulkan/model_downloader_ps5.cpp
 vulkan/ui/backend.cpp vulkan/ui/program.cpp)
while IFS= read -r relative; do
 [[ -n $relative && $relative != \#* ]] || continue
 [[ $relative != gfx/gl_program.cpp && $relative != platform/ps5/display_egl.cpp ]] || continue
 sources+=("${kit_stage#"$root/"}/src/$relative")
done < "$root/ui-kit/sources.txt"
python3 "$root/tools/prepare-hybrid-media.py"
for relative in "${sources[@]}"; do
 media_includes=()
 case "$relative" in
  src/ps5_agc_backend.cpp|src/sd_runtime_ps5.cpp) media_includes=(-I "$root/vendor/include");;
 esac
 object="$work/obj/${relative//\//_}.o"
 ninja_inputs=("$root/$relative" "$root/tooling/prospero-clang18")
 ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk" USE_CCACHE="${USE_CCACHE:-1}" sh "$root/tooling/prospero-clang18" \
  -std=c++20 -O2 -fexceptions -fcxx-exceptions -frtti -Wall -Wextra -Wno-missing-field-initializers \
  -ffunction-sections -fdata-sections -DPS5_LLAMA_VULKAN -DPROSPERO_UI_VULKAN -DPROSPERO_HYBRID_MEDIA \
  -DPS5_MEDIA_AUDIO -DPS5_MEDIA_IMAGE -DPS5_SANDBOX_APP -DPS5_APP_HAS_DSO_HANDLE -DPS5_AGC_LINKED -DPS5_AGC_STABLE_AUDIO -DPS5_SD_GENERATE -DCL_TARGET_OPENCL_VERSION=120 -DGL_GLEXT_PROTOTYPES \
  -DSDL_MAIN_HANDLED -DSDL_STATIC_LIB -DUSING_GENERATED_CONFIG_H \
  "${media_includes[@]}" -I "$root/include" -I "$root/vulkan" -I "$root/vulkan/ui" -I "$kit_stage/src" -I "$work/generated" \
  -I "$root/vendor/ps5/sdl/include" -I "$root/vendor/ps5/sdl/include/SDL2" \
  -I "$root/.deps/llama.cpp/vendor" -I "$root/.deps/llama.cpp/include" -I "$root/.deps/llama.cpp/ggml/include" \
  -I "$root/vendor/include" -I "$root/src" -I "$root/.deps/Vulkan-Headers/include" -I "$root/.deps/ps5-opengl/current/include" \
  -MD -MF "$object.d" -c "$root/$relative" -o "$object"
 objects+=("$object")
done
ninja_run
for index in "${!objects[@]}"; do
 case "${objects[$index]}" in
  *src_ps5_agc_backend.cpp.o|*src_sd_runtime_ps5.cpp.o)
   llvm-objcopy-18 --redefine-syms="$work/media/symbols.map" "${objects[$index]}" "${objects[$index]%.o}.sd.o"
   objects[$index]="${objects[$index]%.o}.sd.o";;
 esac
done
for name in app_crt app_cpp_runtime; do
 cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -c "$ref/tooling/native/$name.cpp" -o "$work/obj/$name.o"
done
cc -std=c11 -O2 -c "$root/tooling/native/llama_ps5_compat.c" -o "$work/obj/llama_compat.o"
for lib in Agc AgcDriver; do
 source=agc_canary_link_stub.c
 [[ $lib != AgcDriver ]] || source=agc_driver_canary_link_stub.c
 cc -std=c11 -O2 -fPIC -c "$ref/vendor/ps5/sdk/stubs/$source" -o "$work/obj/$lib.o"
 stub_objects=("$work/obj/$lib.o")
 if [[ $lib == Agc ]]; then
  cc -std=c11 -O2 -fPIC -c "$root/tooling/native/prosperoai_import_stub_hybrid_agc.c" -o "$work/obj/AgcMedia.o"
  stub_objects+=("$work/obj/AgcMedia.o")
 fi
 "$sdk/bin/prospero-lld" --shared -soname "libSce$lib.prx" -o "$work/stubs/libSce$lib.so" "${stub_objects[@]}"
done
cc -O2 -fPIC -c "$root/tooling/native/prosperoai_import_stub_common_dialog.cpp" -o "$work/obj/CommonDialog.o"
"$sdk/bin/prospero-lld" --shared -soname libSceCommonDialog.prx -o "$work/stubs/libSceCommonDialog.so" "$work/obj/CommonDialog.o"
source "$ref/tools/radv-link.sh"
export PS5_CLANG=${PS5_CLANG:-clang-18}
radv_link_recipe "$ref" "$sdk" "$archive"
for symbol in pthread_once strtof strtod fseek ftell strcasestr; do radv_link_flags+=("--defsym=$symbol=prospero_$symbol"); done
cat > "$work/app-compat.map" <<'MAP'
{ local: pthread_once; strtof; strtod; fseek; ftell; strcasestr; gl*; hui_release_splash; };
MAP
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" --wrap=sceAgcInit \
 --version-script "$work/app-compat.map" --version-script "$ref/tooling/native/app-symbols.map" --exclude-libs=ALL -e _start \
 -o "$work/llvm-pie.elf" "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "${objects[@]}" "$work/obj/llama_compat.o" \
 "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "$work/stubs/libSceCommonDialog.so" \
 --start-group "$root/vendor/ps5/sdl/lib/libSDL2.a" "$root/build/llama-ps5/src/libllama.a" \
 "$root/build/llama-ps5/ggml/src/libggml.a" "$root/build/llama-ps5/ggml/src/libggml-base.a" \
 "$root/build/llama-ps5/ggml/src/libggml-cpu.a" "$root/build/llama-ps5/ggml/src/ggml-vulkan/libggml-vulkan.a" \
 "$work/media/libstable-diffusion.a" "$work/media/libggml.a" "$work/media/libggml-cpu.a" "$work/media/libggml-base.a" \
 "$root/vendor/lib/libstable-audio.a" "$root/vendor/lib/libkokoro-tts.a" "$root/vendor/lib/libespeak-ng.a" \
 "$root/vendor/lib/libtts-ggml.a" "$root/vendor/lib/libtts-ggml-cpu.a" "$root/vendor/lib/libtts-ggml-base.a" "$root/vendor/lib/libcompat.a" --end-group \
 --defsym=vkGetInstanceProcAddr=radv_GetInstanceProcAddr --defsym=vkGetDeviceProcAddr=vk_common_GetDeviceProcAddr \
 --defsym=vkCmdCopyBuffer=vk_common_CmdCopyBuffer --defsym=vkGetPhysicalDeviceFeatures2=vk_common_GetPhysicalDeviceFeatures2 \
 --defsym=execlp=prospero_execlp --defsym=posix_madvise=prospero_posix_madvise \
 "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
"$root/build/host/ps5-native-tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" --stub-dir "$sdk/target/lib" \
 --stub "$work/stubs/libSceAgc.so" --stub "$work/stubs/libSceAgcDriver.so" --stub "$work/stubs/libSceCommonDialog.so" \
 --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$root/build/host/ps5-native-tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$root/runtime/libc.prx" "$app/sce_module/"
cp -a "$root/sce_sys/." "$app/sce_sys/"
python3 - "$app/sce_sys/param.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1]));p.update(titleId='PPSA99023',conceptId='99023',contentId='UP9000-PPSA99023_00-PROSPEROVKSMOKE1X')
with open(sys.argv[1],'w') as f:json.dump(p,f,indent=2)
PY
mkdir -p "$app/assets/fonts" "$app/assets/audio/sfx"
cp "$fonts/"*.huifont "$fonts/"*.txt "$app/assets/fonts/"
cp -a "$kit/assets/audio/sfx/." "$app/assets/audio/sfx/"
mkdir -p "$app/assets/web/fonts"
cp -a "$root/assets/web/." "$app/assets/web/"
for font in Inter-Regular.ttf Inter-SemiBold.ttf Montserrat-Medium.ttf Inter-LICENSE.txt Montserrat-LICENSE.txt; do
    cp "$root/.deps/ui-kit/checkout/third_party/fonts/$font" "$app/assets/web/fonts/"
done
for sounds in "$app/assets/audio/sfx/"*/; do
 python3 - "$sounds" <<'PYINDEX'
from pathlib import Path
import sys
p=Path(sys.argv[1]);(p/'index.txt').write_text(''.join(f.name+'\n' for f in sorted(p.glob('*.wav'))))
PYINDEX
done
echo "Native Vulkan app folder: $app"
