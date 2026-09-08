// ProsperoAI - Native PlayStation 5 local AI application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

enum gpt_input_key_t {
    GPT_INPUT_CROSS,
    GPT_INPUT_CIRCLE,
    GPT_INPUT_SQUARE,
    GPT_INPUT_TRIANGLE,
    GPT_INPUT_OPTIONS,
    GPT_INPUT_L1,
    GPT_INPUT_R1,
    GPT_INPUT_UP,
    GPT_INPUT_DOWN,
    GPT_INPUT_LEFT,
    GPT_INPUT_RIGHT,
    GPT_INPUT_SCROLL_UP,
    GPT_INPUT_SCROLL_DOWN,
    GPT_INPUT_TEXT,
    GPT_INPUT_BACKSPACE,
    GPT_INPUT_ENTER,
    GPT_INPUT_COUNT
};

struct gpt_input_event_t {
    gpt_input_key_t key;
    bool pressed;
    char text;
};

bool gpt_input_init(void);
void gpt_input_poll(void);
bool gpt_input_next(gpt_input_event_t * event);
bool gpt_input_pressed(gpt_input_key_t key);
void gpt_input_shutdown(void);
