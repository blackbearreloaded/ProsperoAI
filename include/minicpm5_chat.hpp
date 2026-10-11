// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpt_runtime.hpp"
#include <cstring>
#include <string>

namespace prospero {
inline std::string trim_newlines(const std::string &text) {
    const auto first = text.find_first_not_of('\n');
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of('\n') - first + 1);
}
inline std::string leading_newlines_removed(const std::string &text) {
    const auto first = text.find_first_not_of('\n');
    return first == std::string::npos ? std::string{} : text.substr(first);
}
// OpenBMB's text-only template, including its explicit BOS.
// Source: MiniCPM5-2B/f97400052a43d642bbc6e9975e2397e3ae6a6b52/chat_template.jinja.
// The runtime message API carries text roles/content, without structured tools.
inline bool minicpm5_chat(const gpt_runtime_message_t *messages, unsigned count, std::string &prompt) {
    prompt = "<s>";
    for (unsigned i = 0; i < count; ++i) {
        if (!messages[i].role || !messages[i].content) return false;
        const std::string role = messages[i].role;
        std::string content = messages[i].content;
        if (role != "system" && role != "user" && role != "assistant") return false;
        if (role == "assistant") {
            const auto end = content.find("</think>");
            if (end != std::string::npos) {
                std::string reasoning = content.substr(0, end);
                const auto start = reasoning.rfind("<think>");
                if (start != std::string::npos) reasoning.erase(0, start + 7);
                reasoning = trim_newlines(reasoning);
                const auto last_end = content.rfind("</think>");
                const std::string answer = leading_newlines_removed(content.substr(last_end + 8));
                content = answer;
                if (!reasoning.empty()) content = "<think>\n" + reasoning + "\n</think>\n\n" + answer;
                else if (content.find("<think>") == std::string::npos && content.find("</think>") == std::string::npos)
                    content = "<think>\n\n</think>\n\n" + leading_newlines_removed(content);
            } else if (content.find("<think>") == std::string::npos) {
                content = "<think>\n\n</think>\n\n" + leading_newlines_removed(content);
            }
        }
        prompt += "<|im_start|>" + role + "\n" + content + "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return true;
}
}
