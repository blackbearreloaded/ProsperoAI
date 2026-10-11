// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>
#include <string>

namespace prospero
{
inline void reasoning_prompt(std::string &prompt, const char *chat_template, bool minicpm5,
                             bool thinking)
{
    if (!minicpm5 && (!chat_template || !std::strstr(chat_template, "<think>")))
        return;
    // The builtin ChatML formatter omits Qwen's enable_thinking generation suffix.
    if (!minicpm5 && chat_template && std::strstr(chat_template, "enable_thinking") &&
        prompt.ends_with("<|im_start|>assistant\n"))
    {
        prompt += thinking ? "<think>\n" : "<think>\n\n</think>\n\n";
    }
    else if (!thinking)
    {
        if (prompt.ends_with("<think>\n"))
            prompt += "</think>\n\n";
        else if (prompt.ends_with("<think>"))
            prompt += "</think>\n\n";
        else
            prompt += "<think>\n</think>\n\n";
    }
}
} // namespace prospero
