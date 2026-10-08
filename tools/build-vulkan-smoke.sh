#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ref="$root/.deps/mihawk-vulkan-review"
sdk="$ref/.deps/native/ps5-payload-sdk"
archive="$ref/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
work="$root/build/vulkan-smoke"
model_test=${LLAMA_SMOKE:-0}
app_test=${PROSPERO_VULKAN_APP:-0}
[[ $app_test == 0 ]] || model_test=1
[[ $model_test == 0 ]] || work="$root/build/llama-vulkan-title"
[[ $app_test == 0 ]] || work="$root/build/prospero-vulkan"
app="$work/PPSA99023"
tool="$root/build/host/ps5-native-tool"
mkdir -p "$work/obj" "$work/stubs" "$app/sce_sys" "$app/sce_module"
cc() { PS5_PAYLOAD_SDK="$sdk" USE_CCACHE=0 sh "$root/tooling/prospero-clang18" "$@"; }
llama_inputs=()
if [[ $model_test == 0 ]]; then
    cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -I "$root/.deps/Vulkan-Headers/include" -c "$root/tools/vulkan-smoke.c" -o "$work/obj/smoke.o"
else
    if [[ $app_test == 0 ]]; then
        cc -std=c++17 -O2 -fexceptions -fcxx-exceptions -ffunction-sections -fdata-sections \
            -I "$root/.deps/llama.cpp/include" -I "$root/.deps/llama.cpp/ggml/include" \
            -c "$root/tools/llama-vulkan-smoke.cpp" -o "$work/obj/smoke.o"
        app_objects=("$work/obj/smoke.o")
    else
        export CCACHE_DIR=${CCACHE_DIR:-$root/build/ccache}
        source "$root/tools/ninja-build.sh"
        ninja_begin "$work/app.ninja"
        app_objects=()
        for name in main gpt_runtime_vulkan gpt_app gpt_input gpt_ime bitmap_font_engine http_server session_store; do
            object="$work/obj/$name.o"
            ninja_inputs=("$root/src/$name.cpp" "$root/tooling/prospero-clang18")
            ninja_edge CXX "$object" env PS5_PAYLOAD_SDK="$sdk" USE_CCACHE="${USE_CCACHE:-1}" \
                sh "$root/tooling/prospero-clang18" -std=c++20 -O2 -fexceptions -fcxx-exceptions -frtti \
                -Wall -Wextra -ffunction-sections -fdata-sections -DPS5_LLAMA_VULKAN \
                -DSDL_MAIN_HANDLED -DSDL_STATIC_LIB -DUSING_GENERATED_CONFIG_H -DRMLUI_STATIC_LIB -DITLIB_FLAT_MAP_NO_THROW \
                -I "$root/include" -I "$root/vendor/ps5/sdl/include" -I "$root/vendor/ps5/sdl/include/SDL2" \
                -I "$root/vendor/ps5/rmlui/include" -I "$root/.deps/llama.cpp/include" -I "$root/.deps/llama.cpp/ggml/include" \
                -MD -MF "$object.d" -c "$root/src/$name.cpp" -o "$object"
            app_objects+=("$object")
        done
        ninja_run
        app_objects+=("$root/vendor/ps5/sdl/lib/libSDL2.a" "$root/vendor/ps5/rmlui/lib/librmlui.a" "$root/vendor/ps5/freetype/lib/libfreetype.a")
        mkdir -p "$app/assets"
        cp -a "$root/assets/." "$app/assets/"
        version=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$root/sce_sys/param.json")
        sed -i "s/{{PROSPERO_AI_VERSION}}/$version/g" "$app/assets/ui/main.rml"
    fi
    cc -std=c11 -O2 -c "$root/tooling/native/llama_ps5_compat.c" -o "$work/obj/llama_compat.o"
    llama_inputs=("$work/obj/llama_compat.o" --start-group "$root/build/llama-ps5/src/libllama.a"
        "$root/build/llama-ps5/ggml/src/libggml.a" "$root/build/llama-ps5/ggml/src/libggml-base.a"
        "$root/build/llama-ps5/ggml/src/libggml-cpu.a" "$root/build/llama-ps5/ggml/src/ggml-vulkan/libggml-vulkan.a" --end-group
        --defsym=vkGetInstanceProcAddr=radv_GetInstanceProcAddr --defsym=vkGetDeviceProcAddr=vk_common_GetDeviceProcAddr
        --defsym=vkCmdCopyBuffer=vk_common_CmdCopyBuffer --defsym=vkGetPhysicalDeviceFeatures2=vk_common_GetPhysicalDeviceFeatures2
    --defsym=execlp=prospero_execlp --defsym=posix_madvise=prospero_posix_madvise)
    mkdir -p "$app/models"
    cp -p -u "${MODEL_GGUF:-$root/build/vulkan-models/stories260K.gguf}" "$app/models/"
fi
for source in app_crt app_cpp_runtime; do
    cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
        -c "$ref/tooling/native/$source.cpp" -o "$work/obj/$source.o"
done
[[ $model_test != 0 ]] || app_objects=("$work/obj/smoke.o")
for lib in Agc AgcDriver; do
    source=agc_canary_link_stub.c
    [[ $lib != AgcDriver ]] || source=agc_driver_canary_link_stub.c
    cc -std=c11 -O2 -fPIC -c "$ref/vendor/ps5/sdk/stubs/$source" -o "$work/obj/$lib.o"
    "$sdk/bin/prospero-lld" --shared -soname "libSce$lib.prx" -o "$work/stubs/libSce$lib.so" "$work/obj/$lib.o"
done
if [[ $app_test != 0 ]]; then
    for lib in agc agc_driver common_dialog; do
        suffix=Agc
        [[ $lib != agc_driver ]] || suffix=AgcDriver
        [[ $lib != common_dialog ]] || suffix=CommonDialog
        source="$root/tooling/native/prosperoai_import_stub_$lib.c"
        [[ $lib != common_dialog ]] || source="$root/tooling/native/prosperoai_import_stub_$lib.cpp"
        cc -O2 -fPIC -c "$source" -o "$work/obj/$lib-extra.o"
        stub_objects=("$work/obj/$lib-extra.o")
        [[ $lib == common_dialog ]] || stub_objects+=("$work/obj/$suffix.o")
        "$sdk/bin/prospero-lld" --shared --allow-multiple-definition -soname "libSce$suffix.prx" \
            -o "$work/stubs/libSce$suffix.so" "${stub_objects[@]}"
    done
    app_objects+=("$work/stubs/libSceCommonDialog.so")
fi
source "$ref/tools/radv-link.sh"
export PS5_CLANG=${PS5_CLANG:-clang-18}
radv_link_recipe "$ref" "$sdk" "$archive"
if [[ $app_test != 0 ]]; then
    for symbol in pthread_once strtof fseek ftell strcasestr; do
        radv_link_flags+=("--defsym=$symbol=prospero_$symbol")
    done
    cat > "$work/app-compat.map" <<'MAP'
{ local: pthread_once; strtof; fseek; ftell; strcasestr; };
MAP
    radv_link_flags+=(--version-script "$work/app-compat.map")
fi
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$ref/tooling/native/app-symbols.map" --exclude-libs=ALL -e _start \
    -o "$work/llvm-pie.elf" "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "${app_objects[@]}" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" "${llama_inputs[@]}" "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so
extra_stubs=()
[[ $app_test == 0 ]] || extra_stubs=(--stub "$work/stubs/libSceCommonDialog.so")
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" --stub-dir "$sdk/target/lib" "${extra_stubs[@]}" \
    --stub "$work/stubs/libSceAgc.so" --stub "$work/stubs/libSceAgcDriver.so" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$root/sce_sys/"{icon0.png,pic0.dds,pic1.dds,snd0.at9} "$app/sce_sys/"
python3 - "$root/sce_sys/param.json" "$app/sce_sys/param.json" <<'PY'
import json,sys
p=json.load(open(sys.argv[1]))
p.update(titleId='PPSA99023',conceptId='99023',contentId='UP9000-PPSA99023_00-PROSPEROVKSMOKE1X')
for v in p['localizedParameters'].values():
    if isinstance(v,dict): v['titleName']='ProsperoAI Vulkan Test'
with open(sys.argv[2],'w') as f: json.dump(p,f,indent=2)
PY
cp "$root/runtime/libc.prx" "$app/sce_module/libc.prx"
if [[ ${VULKAN_PACKAGE:-1} == 0 ]]; then
    echo "Vulkan app folder: $app"
    exit 0
fi
mkpfs=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpfsc)
package="$work/PPSA99023.$$.ffpfsc"
"$mkpfs" pack folder --no-adjust-output-file-extension --version PS5 --verify "$app" "$package"
mv "$package" "$work/PPSA99023.ffpfsc"
echo "Vulkan smoke package: $work/PPSA99023.ffpfsc"
