// ProsperoAI - The AGC text runtime inside the Vulkan build.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpt_runtime.hpp"

// src/gpt_runtime.cpp compiled for text only (vulkan/gpt_runtime_agc_text.cpp): the
// model.ps5lm bundles that ProsperoAI 01.000.000 installed keep working beside the
// GGUF files the Vulkan runtime reads. The hybrid runtime owns the choice between them.
namespace prospero_agc
{
bool gpt_runtime_available();
const char *gpt_runtime_name();
const char *gpt_runtime_backend();
const char *gpt_runtime_purpose();
unsigned gpt_runtime_model_count();
unsigned gpt_runtime_selected_model();
const char *gpt_runtime_model_id(unsigned index);
const char *gpt_runtime_model_name(unsigned index);
const char *gpt_runtime_model_purpose(unsigned index);
bool gpt_runtime_select_model(unsigned index);
void gpt_runtime_refresh_models();
int gpt_runtime_prepare();
bool gpt_runtime_context_full();
int gpt_runtime_generate(const gpt_runtime_message_t *messages, unsigned message_count,
                         const gpt_runtime_settings_t &settings, char *output,
                         std::size_t output_capacity, gpt_runtime_stats_t *stats,
                         gpt_runtime_progress_fn progress);

// Takes the resident model out of memory; the next answer loads it again.
void release_model_memory();
} // namespace prospero_agc
