# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.18)
project(prospero_vulkan_runtime LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(PROS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PROS_LLAMA_SOURCE "${PROS_ROOT}/.deps/llama.cpp" CACHE PATH "Shared llama/ggml source")
option(PROS_PRISM_RUNTIME "Build the complete pinned Prism runtime" OFF)
if(PROS_PRISM_RUNTIME)
    add_compile_definitions(PROSPERO_PRISM_RUNTIME)
endif()
# One source tree, tensor ABI and backend for every producer and consumer.
add_compile_definitions(GGML_MAX_NAME=160)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN ON CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROS_LLAMA_SOURCE}" llama)
set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SD_USE_SYSTEM_GGML ON CACHE BOOL "" FORCE)
set(SD_USE_UPSTREAM_GGML ON CACHE BOOL "" FORCE)
set(SD_GGML_SOURCE_DIR "${PROS_LLAMA_SOURCE}/ggml" CACHE PATH "" FORCE)
set(SD_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory("${PROS_ROOT}/.deps/stable-diffusion-vulkan" sd)
target_compile_features(stable-diffusion PRIVATE cxx_std_20)
target_compile_options(stable-diffusion PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/sd-constant-tls.hpp>")
if(PROS_PRISM_RUNTIME)
    target_compile_options(stable-diffusion PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/prism-sd-compat.hpp>")
endif()
# The source archive has no .git; do not let its version report the parent app's commit.
set_property(SOURCE "${PROS_ROOT}/.deps/stable-diffusion-vulkan/src/version.cpp"
    DIRECTORY "${PROS_ROOT}/.deps/stable-diffusion-vulkan"
    PROPERTY COMPILE_DEFINITIONS
    SDCPP_BUILD_COMMIT=f89d9b13d730eabeede7314ce49dacd18d3c90c2 SDCPP_BUILD_VERSION=prospero-pinned)
# Speech on the same ggml/Vulkan backend. kokoro.cpp reuses the ggml target added above
# instead of its bundled submodule, and espeak-ng comes from the pinned headers plus the
# prebuilt PS5 archive, because the cross environment has no system copy.
set(KOKORO_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(KOKORO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KOKORO_VULKAN ON CACHE BOOL "" FORCE)
set(KOKORO_STATIC ON CACHE BOOL "" FORCE)
set(KOKORO_GGML_DIR "${PROS_LLAMA_SOURCE}/ggml" CACHE PATH "" FORCE)
set(ESPEAK_NG_INCLUDEDIR "${PROS_ROOT}/.deps/espeak-ng/src/include" CACHE PATH "" FORCE)
if(CMAKE_CROSSCOMPILING)
    set(ESPEAK_NG_LIB "${PROS_ROOT}/vendor/lib/libespeak-ng.a" CACHE FILEPATH "" FORCE)
    # Without this, pkg-config finds the host's libespeak-ng-dev and drags /usr/include into
    # the cross compile, where <stdint.h> then resolves to the host glibc header.
    set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON)
    set(ESPEAK_NG_FOUND TRUE)
    add_library(PkgConfig::ESPEAK_NG STATIC IMPORTED)
    set_target_properties(PkgConfig::ESPEAK_NG PROPERTIES
        IMPORTED_LOCATION "${ESPEAK_NG_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${ESPEAK_NG_INCLUDEDIR}")
endif()
if(CMAKE_CROSSCOMPILING)
    # The console is one fixed CPU, so Highway needs no runtime dispatch. Compiling only the
    # static target also avoids its AVX-512 code paths, which the x86_64-sie-ps5 target
    # rejects, and AVX2/FMA/F16C match what ggml is already built with here.
    add_compile_definitions(HWY_COMPILE_ONLY_STATIC=1)
    add_compile_options(-mavx2 -mfma -mf16c)
endif()
add_subdirectory("${PROS_ROOT}/.deps/kokoro.cpp" kokoro)

include("${CMAKE_CURRENT_LIST_DIR}/sd-portability.cmake")
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(vulkan-runtime-abi-test "${PROS_ROOT}/tests/vulkan_runtime_abi_test.cpp")
    target_link_libraries(vulkan-runtime-abi-test PRIVATE llama stable-diffusion ggml-vulkan)
    target_compile_features(vulkan-runtime-abi-test PRIVATE cxx_std_20)
    target_compile_options(vulkan-runtime-abi-test PRIVATE -UNDEBUG
        "-include${CMAKE_CURRENT_LIST_DIR}/sd-constant-tls.hpp")
    add_executable(vulkan-runtime-media-test "${PROS_ROOT}/tests/media_vulkan_test.cpp"
        "${PROS_ROOT}/inference/backends/vulkan/media_bridge.cpp")
    target_include_directories(vulkan-runtime-media-test BEFORE PRIVATE
        "${PROS_LLAMA_SOURCE}/ggml/include" "${PROS_ROOT}/include" "${PROS_ROOT}/vendor/include")
    target_compile_options(vulkan-runtime-media-test PRIVATE -UNDEBUG)
    target_link_libraries(vulkan-runtime-media-test PRIVATE ggml-vulkan ggml)
endif()
