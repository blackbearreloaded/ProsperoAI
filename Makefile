# ps5-native-app-boilerplate - Linux/WSL build entry points.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := /bin/bash
.DEFAULT_GOAL := app

-include .env

APP_DEFINITIONS ?= PS5_DUAL_BACKEND PS5_MEDIA_AUDIO PS5_MEDIA_IMAGE PS5_SANDBOX_APP PS5_APP_HAS_DSO_HANDLE PS5_AGC_LINKED PS5_AGC_STABLE_AUDIO PS5_SD_GENERATE PS5_WRAP_MALLOC CL_TARGET_OPENCL_VERSION=120
APP_CXXFLAGS ?=
APP_INCLUDE_PATHS ?= vendor/include src
MEDIA_ARCHIVES := vendor/lib/libstable-diffusion.a vendor/lib/libggml.a vendor/lib/libggml-cpu.a vendor/lib/libggml-base.a vendor/lib/libstable-audio.a vendor/lib/libkokoro-tts.a vendor/lib/libespeak-ng.a vendor/lib/libpocket-tts-runtime.a vendor/lib/libllama.a vendor/lib/libmtmd.a vendor/lib/libvendor-hash.a vendor/lib/libtts-ggml.a vendor/lib/libtts-ggml-cpu.a vendor/lib/libtts-ggml-base.a vendor/lib/libcompat.a
APP_STATIC_ARCHIVES ?= $(MEDIA_ARCHIVES) $(MEDIA_ARCHIVES) $(MEDIA_ARCHIVES)
APP_RUNTIME_MODULES ?=
PACBREW_PACKAGES ?=
PACBREW_INCLUDE_PATHS ?=
PACBREW_STATIC_ARCHIVES ?=
PS5_HOST ?=
FTP_PORT ?= 2121
DEPLOY_FORMAT ?= folder
PS5_FTP_USER ?= anonymous
PS5_FTP_PASSWORD ?= codex
DEPLOY_DRY_RUN ?= 0
TITLE_ID ?=
APP_NAME ?=
APP_CATEGORY ?= game
CONTENT_SUFFIX ?=
HOST_CXX ?= clang++
HOST_TEST_CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror \
	-ffunction-sections -fdata-sections
HOST_TEST_LDFLAGS ?= -Wl,--gc-sections
GTEST_ARGS ?=
BUILD_JOBS ?= $(shell nproc 2>/dev/null || echo 2)
USE_CCACHE ?= 1
export BUILD_JOBS USE_CCACHE
CCACHE_DIR ?= $(CURDIR)/build/ccache
export CCACHE_DIR
export HOST_CXX HOST_TEST_CXXFLAGS HOST_TEST_LDFLAGS
export APP_DEFINITIONS APP_CXXFLAGS APP_INCLUDE_PATHS APP_STATIC_ARCHIVES APP_RUNTIME_MODULES
export PACBREW_PACKAGES PACBREW_INCLUDE_PATHS PACBREW_STATIC_ARCHIVES
export PS5_HOST FTP_PORT DEPLOY_FORMAT PS5_FTP_USER PS5_FTP_PASSWORD DEPLOY_DRY_RUN
export TITLE_ID APP_NAME APP_CATEGORY CONTENT_SUFFIX

RUNTIME := runtime/libc.prx
RUNTIME_INPUTS := tools/rebuild-libc.sh tools/build-host-tools.sh tools/ninja-build.sh \
	$(wildcard tooling/native/*.cpp tooling/native/*.hpp) \
	$(wildcard tooling/native/runtime/*.txt)
APP_DEFINITIONS += SDL_MAIN_HANDLED SDL_STATIC_LIB USING_GENERATED_CONFIG_H RMLUI_STATIC_LIB ITLIB_FLAT_MAP_NO_THROW
APP_CXXFLAGS += -frtti
APP_INCLUDE_PATHS += .deps/llama.cpp/vendor include vendor/ps5/sdl/include vendor/ps5/sdl/include/SDL2 vendor/ps5/rmlui/include
APP_STATIC_ARCHIVES += vendor/ps5/sdl/lib/libSDL2.a vendor/ps5/rmlui/lib/librmlui.a vendor/ps5/freetype/lib/libfreetype.a vendor/ps5/sdk/lib/libunwind.a vendor/ps5/sdk/lib/libcxx.a vendor/ps5/sdk/lib/libcxxabi.a

.PHONY: all app build init doctor test test-deps test-unit test-integration libc deps deps-status llama-vulkan radv pacbrew pacbrew-list assets-check format format-check tidy lint check ffpkg ffpfsc packages deploy undeploy clean distclean help

all: app
build: app

init:
	@printf '%s\n' '==> [init] Configuring the application identity in sce_sys/param.json'
	@bash tools/init-project.sh sce_sys/param.json

doctor:
	@printf '%s\n' '==> [doctor] Checking the Linux/WSL host without changing it'
	@bash tools/doctor.sh

test: test-integration

test-deps:
	@printf '%s\n' '==> [test-deps] No host-only dependencies are required'

test-unit: assets-check

test-integration:
	@printf '%s\n' '==> [test-integration] Running host tooling integration tests'
	@python3 -m unittest discover -s tests -p 'test_*.py' -v

deps: test-deps
	@printf '%s\n' '==> [deps] Fetching declared native dependencies'
	@bash tools/setup-native-dependencies.sh
	@bash tools/setup-pacbrew-dependencies.sh --environment
	@python3 -B tools/deps.py fetch

deps-status:
	@python3 -B tools/deps.py status

radv: deps ## Build RADV for the PS5 (libvulkan_radeon.ps5.a) from the pinned Mihawk checkouts
	@bash tools/build-radv.sh

llama-vulkan: deps ## Host llama-cli with the Vulkan backend, plus a PS5 syntax check of ggml-vulkan
	@bash tools/build-llama-vulkan.sh

.PHONY: llama-ps5 vulkan-smoke llama-vulkan-title app-vulkan app-vulkan-folder
llama-ps5: deps ## Cross-build static llama.cpp and Vulkan libraries for PS5
	@bash tools/build-llama-ps5.sh

vulkan-smoke: ## Package PS5 Vulkan startup/device-discovery test (PPSA99023)
	@bash tools/build-vulkan-smoke.sh

llama-vulkan-title: llama-ps5 ## Package model inference test (requires build/vulkan-models/stories260K.gguf)
	@LLAMA_SMOKE=1 bash tools/build-vulkan-smoke.sh

app-vulkan: llama-ps5 ## Build ProsperoAI UI/API with llama.cpp Vulkan in safe test slot PPSA99023
	@PROSPERO_VULKAN_APP=1 bash tools/build-vulkan-smoke.sh

app-vulkan-folder: llama-ps5 ## Build the Vulkan app folder without image compression
	@PROSPERO_VULKAN_APP=1 VULKAN_PACKAGE=0 bash tools/build-vulkan-smoke.sh

pacbrew:
	@printf '%s\n' '==> [pacbrew] Fetching the pinned prebuilt ports sysroot'
	@bash tools/setup-pacbrew-dependencies.sh --all

pacbrew-list:
	@printf '%s\n' '==> [pacbrew] Listing available pkg-config modules'
	@bash tools/setup-pacbrew-dependencies.sh --list

assets-check:
	@printf '%s\n' '==> [assets] Validating icon, backgrounds, and selection audio'
	@bash tools/validate-assets.sh

libc:
	@printf '%s\n' '==> [libc] Rebuilding and verifying the clean-room runtime'
	@bash tools/rebuild-libc.sh

$(RUNTIME): $(RUNTIME_INPUTS)
	@printf '%s\n' '==> [libc] Generating the missing or outdated runtime'
	@bash tools/rebuild-libc.sh

app: $(RUNTIME)
	@printf '%s\n' '==> [app] Compiling, linking, signing, and assembling the app folder'
	@bash tools/build.sh Folder

ffpkg: $(RUNTIME)
	@printf '%s\n' '==> [ffpkg] Building the app folder and UFS2 image'
	@bash tools/build.sh Ffpkg

ffpfsc: $(RUNTIME)
	@printf '%s\n' '==> [ffpfsc] Building the app folder and compressed image'
	@bash tools/build.sh Ffpfsc

packages: $(RUNTIME)
	@printf '%s\n' '==> [packages] Building the app folder and both package formats'
	@bash tools/build.sh All

deploy:
	@printf '%s\n' '==> [deploy] Building and publishing the selected app output over FTP'
	@bash tools/deploy.sh

undeploy:
	@printf '%s\n' '==> [undeploy] Removing staged development files for this title over FTP'
	@bash tools/deploy.sh undeploy

format:
	@printf '%s\n' '==> [format] Formatting C and C++ sources'
	@bash tools/run_clang_format.sh

format-check:
	@printf '%s\n' '==> [format] Checking C and C++ formatting'
	@bash tools/run_clang_format.sh --check

tidy:
	@printf '%s\n' '==> [tidy] Running Clang static analysis'
	@bash tools/run_clang_tidy.sh

lint:
	@printf '%s\n' '==> [lint] Running source, metadata, and shell checks'
	@bash tools/lint.sh

check: lint test app

clean:
	@printf '%s\n' '==> [clean] Removing generated build outputs'
	@rm -rf -- build dist
	@rm -f -- $(RUNTIME)

distclean: clean
	@printf '%s\n' '==> [distclean] Removing downloaded dependency caches'
	@rm -rf -- .deps

help:
	@printf '%s\n' \
	  'make                 Generate libc.prx and build the Hello World folder' \
	  'make init TITLE_ID=PPSA12345 APP_NAME="My App"  Configure app identity' \
	  'make doctor          Check required and optional Linux/WSL tools' \
	  'make test            Run host integration tests' \
	  'make test-deps       Fetch verified host-only GoogleTest source' \
	  'make test-unit       Run host-native GoogleTest application tests' \
	  'make test-integration  Run host tooling integration tests' \
	  'make deps            Fetch native and pinned dependencies (tools/deps.json)' \
	  'make deps-status     Show each pinned dependency and whether it matches its pin' \
	  'make llama-vulkan    Build llama.cpp with Vulkan on the host and syntax-check it for the PS5' \
	  'make radv            Build RADV for the PS5 from the pinned Mihawk checkouts' \
	  'make pacbrew         Fetch the pinned PacBrew ports sysroot' \
	  'make pacbrew-list    List PacBrew pkg-config module names' \
	  'make assets-check    Validate the current presentation assets' \
	  'make libc            Force a deterministic runtime/libc.prx rebuild' \
	  'make format          Apply the shared Clang formatting policy' \
	  'make format-check    Check formatting without modifying files' \
	  'make tidy            Run the shared Clang static-analysis policy' \
	  'make lint            Run format, metadata, and shell checks (no clang-tidy)' \
	  'make check           Run lint and build ProsperoAI' \
	  'make ffpkg           Build the folder and UFS2 .ffpkg image' \
	  'make ffpfsc          Build the folder and compressed .ffpfsc image' \
	  'make packages        Build folder, .ffpkg, and .ffpfsc outputs' \
	  'make deploy PS5_HOST=<address>  Build and FTP-deploy the app folder' \
	  'make undeploy PS5_HOST=<address>  Remove this title from /data/homebrew' \
	  'Build variables:     APP_DEFINITIONS, APP_INCLUDE_PATHS, APP_STATIC_ARCHIVES, APP_RUNTIME_MODULES' \
	  'PacBrew variables:   PACBREW_PACKAGES, PACBREW_INCLUDE_PATHS, PACBREW_STATIC_ARCHIVES' \
	  'Deploy variables:    FTP_PORT=2121, DEPLOY_FORMAT=folder|ffpfsc|ffpkg, DEPLOY_DRY_RUN=0|1' \
	  'Local defaults:      Copy .env.example to the ignored .env file' \
	  'Build speed:         BUILD_JOBS defaults to all CPUs; USE_CCACHE=0 disables ccache' \
	  'make clean           Remove build/, dist/, and generated libc.prx' \
	  'make distclean       Also remove the ignored .deps/ cache'
