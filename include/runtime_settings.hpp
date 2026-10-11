// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#ifndef PROSPERO_SETTINGS_PATH
#define PROSPERO_SETTINGS_PATH "/download0/prosperoai.cfg"
#endif
namespace prospero_settings {
struct Runtime { unsigned context = 0, kv = 0; bool thinking = false; };
inline std::mutex mutex;
inline bool valid_context(unsigned n) {
    return n == 0 || n == 4096 || n == 8192 || n == 16384 || n == 32768 || n == 65536 ||
           n == 131072 || n == 262144;
}
inline Runtime read_unlocked() {
    Runtime p;
    if (auto *f = std::fopen(PROSPERO_SETTINGS_PATH, "rb")) {
        char line[256]; unsigned v;
        while (std::fgets(line, sizeof(line), f)) {
            if (std::sscanf(line, "context=%u", &v) == 1 && valid_context(v)) p.context = v;
            // Legacy kv= values are ignored: cache precision is automatic.
            else if (std::sscanf(line, "thinking=%u", &v) == 1) p.thinking = v != 0;
        }
        std::fclose(f);
    }
    return p;
}
inline Runtime get() { std::lock_guard<std::mutex> lock(mutex); return read_unlocked(); }
inline bool set_unlocked(Runtime p) {
    if (!valid_context(p.context) || p.kv != 0) return false;
    std::string contents;
    if (auto *f = std::fopen(PROSPERO_SETTINGS_PATH, "rb")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f))
            if (std::strncmp(line, "context=", 8) && std::strncmp(line, "kv=", 3) && std::strncmp(line, "thinking=", 9)) contents += line;
        std::fclose(f);
    }
    if (!contents.empty() && contents.back() != '\n') contents += '\n';
    char fields[96]; std::snprintf(fields, sizeof(fields), "context=%u\nkv=%u\nthinking=%u\n", p.context, p.kv, p.thinking ? 1U : 0U);
    contents += fields;
    auto *f = std::fopen(PROSPERO_SETTINGS_PATH ".tmp", "wb");
    if (!f) return false;
    const bool written = std::fwrite(contents.data(), 1, contents.size(), f) == contents.size();
    const int closed = std::fclose(f);
    if (!written || closed || std::rename(PROSPERO_SETTINGS_PATH ".tmp", PROSPERO_SETTINGS_PATH)) { std::remove(PROSPERO_SETTINGS_PATH ".tmp"); return false; }
    return true;
}
inline bool set(Runtime p) { std::lock_guard<std::mutex> lock(mutex); return set_unlocked(p); }
inline const char *kv_name(unsigned n) { constexpr const char *names[] = {"auto", "f16", "q8_0", "q4_0"}; return names[n <= 3 ? n : 0]; }
}
