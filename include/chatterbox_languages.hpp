// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The languages the pinned Chatterbox model can speak. The codes mirror audio.cpp's
// supported_chatterbox_language_codes(); anything other than "en" takes its multilingual
// tokenizer path, so a code this build does not know would silently change the encoding.
// English stays first: index 0 is the default and keeps the previous behaviour.
#include <cstddef>
#include <cstring>

namespace prospero_language {

struct Language
{
    const char *code;
    const char *name;
};

inline constexpr Language chatterbox[] = {
    {"en", "English"},   {"ar", "Arabic"}, {"da", "Danish"},     {"de", "German"},
    {"el", "Greek"},     {"es", "Spanish"}, {"fi", "Finnish"},   {"fr", "French"},
    {"hi", "Hindi"},     {"it", "Italian"}, {"ko", "Korean"},    {"ms", "Malay"},
    {"nl", "Dutch"},     {"no", "Norwegian"}, {"pl", "Polish"},  {"pt", "Portuguese"},
    {"sv", "Swedish"},   {"sw", "Swahili"}, {"tr", "Turkish"},
};

inline constexpr unsigned chatterbox_count =
    static_cast<unsigned>(sizeof(chatterbox) / sizeof(chatterbox[0]));

inline bool valid_chatterbox_language(unsigned index) { return index < chatterbox_count; }

inline const Language &chatterbox_language(unsigned index)
{
    return chatterbox[index < chatterbox_count ? index : 0];
}

// -1 when the code is not one this build supports, so callers can refuse it.
inline int chatterbox_language_index(const char *code)
{
    if (!code)
        return -1;
    for (unsigned i = 0; i < chatterbox_count; ++i)
        if (std::strcmp(chatterbox[i].code, code) == 0)
            return static_cast<int>(i);
    return -1;
}

} // namespace prospero_language
