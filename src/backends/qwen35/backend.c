// Qwen3.5 backend compiled into the multi-architecture runtime.
// SPDX-License-Identifier: GPL-3.0-or-later

#define PS5_MODEL_QWEN35 1
#define PS5_STAGED_LAYER 1
#define PS5_SPLIT_ATTN 1
#define PS5_COOPERATIVE_GATE_UP 1
#define PS5_COOPERATIVE_FF_DOWN 1
#define PS5_PRENORMALIZED_LOGITS 1
#define PS5_BACKEND_PREFIX qwen35

#include "model_backend_prefix.h"
#include "chat_prompt_backend.inc"
#include "token_step_backend.inc"
