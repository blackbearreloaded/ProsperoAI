// ProsperoAI model-runtime boundary.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

struct gpt_runtime_message_t {
    const char* role;
    const char* content;
};

struct gpt_runtime_settings_t {
    unsigned response_style;
    unsigned max_output_tokens;
};

struct gpt_runtime_stats_t {
    unsigned prompt_tokens;
    unsigned generated_tokens;
    unsigned reused_tokens;
    std::uint64_t prefill_microseconds;
    std::uint64_t elapsed_microseconds;
};

using gpt_runtime_progress_fn = void (*)(const char* text);

bool gpt_runtime_available();
const char* gpt_runtime_name();
const char* gpt_runtime_backend();
const char* gpt_runtime_purpose();
unsigned gpt_runtime_model_count();
unsigned gpt_runtime_selected_model();
const char* gpt_runtime_model_id(unsigned index);
const char* gpt_runtime_model_name(unsigned index);
const char* gpt_runtime_model_purpose(unsigned index);
bool gpt_runtime_select_model(unsigned index);
int gpt_runtime_generate(const gpt_runtime_message_t* messages,
                         unsigned message_count,
                         const gpt_runtime_settings_t& settings, char* output,
                         std::size_t output_capacity,
                         gpt_runtime_stats_t* stats,
                         gpt_runtime_progress_fn progress);
