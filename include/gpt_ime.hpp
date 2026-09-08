// ProsperoAI - Native PlayStation 5 local AI application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>

using gpt_ime_result_fn = void (*)(const char * text, void * user_data);

bool gpt_ime_init(void);
void gpt_ime_request(const char * initial_text, gpt_ime_result_fn callback,
                       void * user_data);
void gpt_ime_poll(void);
bool gpt_ime_active(void);
void gpt_ime_cancel(void);
void gpt_ime_shutdown(void);
