// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstring>
#include <string>

namespace prospero {
// Invalid or oversized input keeps the current setting. Read only at model load.
inline unsigned read_inference_setting(const char *root, const char *name,
                                       unsigned fallback, unsigned minimum, unsigned maximum) {
    const std::string path = std::string(root) + "/" + name;
    FILE *file = std::fopen(path.c_str(), "r");
    if (!file) return fallback;
    char text[64];
    const auto size = std::fread(text, 1, sizeof(text), file);
    const bool failed = std::ferror(file) || size == sizeof(text);
    std::fclose(file);
    if (failed) return fallback;
    unsigned value = 0;
    bool digits = false, trailing = false;
    for (std::size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (digits) trailing = true;
        } else if (c >= '0' && c <= '9' && !trailing) {
            const unsigned digit = static_cast<unsigned>(c - '0');
            if (digit > maximum || value > (maximum - digit) / 10) return fallback;
            value = value * 10 + digit;
            digits = true;
        } else return fallback;
    }
    return digits && value >= minimum ? value : fallback;
}
}
