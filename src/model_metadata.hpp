#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

inline bool ps5_model_string_from_json(const char *json, const char *key, char *output,
                                       std::size_t capacity)
{
    if (!json || !key || !*key || !output || capacity < 2)
        return false;
    char quoted_key[32];
    const int key_bytes = std::snprintf(quoted_key, sizeof(quoted_key), "\"%s\"", key);
    if (key_bytes <= 2 || static_cast<std::size_t>(key_bytes) >= sizeof(quoted_key))
        return false;
    const char *at = std::strstr(json, quoted_key);
    if (!at || !(at = std::strchr(at + key_bytes, ':')))
        return false;
    do
        ++at;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n');
    if (*at++ != '"')
        return false;

    std::size_t length = 0;
    while (*at && *at != '"' && length + 1 < capacity)
    {
        unsigned char value = static_cast<unsigned char>(*at++);
        if (value == '\\')
        {
            value = static_cast<unsigned char>(*at++);
            if (value != '"' && value != '\\' && value != '/')
                return false;
        }
        if (value < 32)
            return false;
        output[length++] = static_cast<char>(value);
    }
    if (*at != '"' || length == 0)
        return false;
    output[length] = '\0';
    return true;
}

inline bool ps5_model_name_from_json(const char *json, char *output, std::size_t capacity)
{
    return ps5_model_string_from_json(json, "name", output, capacity);
}

inline bool ps5_model_purpose_from_json(const char *json, char *output, std::size_t capacity)
{
    if (!ps5_model_string_from_json(json, "purpose", output, capacity))
        return false;
    return std::strcmp(output, "text-to-text") == 0 || std::strcmp(output, "text-to-image") == 0 ||
           std::strcmp(output, "text-to-audio") == 0 || std::strcmp(output, "text-to-speech") == 0;
}

inline bool ps5_model_runtime_from_json(const char *json, char *output, std::size_t capacity)
{
    return ps5_model_string_from_json(json, "runtime", output, capacity);
}
