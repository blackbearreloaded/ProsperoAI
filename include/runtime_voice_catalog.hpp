// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "generated_voice_catalog.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <optional>
#include <unistd.h>
#if defined(PROSPERO_UI_VULKAN) && !defined(PROSPERO_HOST)
#include "storage.hpp"
#endif
#include <string>
#include <sys/stat.h>
#include <vector>

namespace prospero_voice {
struct RuntimeVoice { std::string id, name, locale, filename; };
inline std::vector<RuntimeVoice> chatterbox_catalog();
inline bool safe_voice_stem(const char *name, std::string &stem);
inline std::string runtime_voice_directory(bool chatterbox) {
#if defined(PROSPERO_UI_VULKAN) && !defined(PROSPERO_HOST)
    return prospero::storage().data_root + "/voices/" + (chatterbox ? "chatterbox" : "kokoro");
#else
    return std::string("/data/prosperoai/voices/") + (chatterbox ? "chatterbox" : "kokoro");
#endif
}
inline bool chatterbox_voice_deletable(const RuntimeVoice &voice) {
    if (voice.id=="custom") return voice.filename=="custom.wav";
    std::string stem;
    if (!safe_voice_stem(voice.filename.c_str(),stem)) return false;
    for (std::size_t i=0;i<chatterbox_count;++i)
        if (voice.id==chatterbox[i].id || voice.filename==chatterbox[i].filename) return false;
    return voice.locale=="Added voice";
}

inline bool safe_voice_stem(const char *name, std::string &stem) {
    const std::string file(name ? name : "");
    const auto dot = file.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return false;
    stem = file.substr(0, dot);
    if (stem.size() > 80 || stem == "." || stem == "..") return false;
    return std::all_of(stem.begin(), stem.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

inline std::vector<RuntimeVoice> runtime_voices(bool chatterbox) {
    std::vector<RuntimeVoice> out;
    if (chatterbox) {
        for (const auto &v : chatterbox_catalog()) out.push_back({v.id,v.name,v.locale,v.filename});
    } else {
        for (std::size_t i=0; i<kokoro_count; ++i) {
            const auto &v=kokoro[i]; out.push_back({v.id,v.name,v.locale,v.filename});
        }
    }
    const std::size_t builtin_count=out.size();
    const std::string dir = runtime_voice_directory(chatterbox);
    if (DIR *d=opendir(dir.c_str())) {
        while (const dirent *e=readdir(d)) {
            if (out.size() >= 120) break;
            std::string stem;
            if (!safe_voice_stem(e->d_name,stem)) continue;
            const std::string file=e->d_name;
            const std::string ext=file.substr(file.find_last_of('.')+1);
            const bool allowed=chatterbox ? (ext=="wav" || ext=="flac") : ext=="gguf";
            if (!allowed || std::any_of(out.begin(),out.end(),[&](const RuntimeVoice &v){return v.id==stem;})) continue;
            struct stat info{};
            if (stat((dir + "/" + file).c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
            out.push_back({stem,stem,"Added voice",file});
        }
        closedir(d);
    }
    std::sort(out.begin()+builtin_count,out.end(),[](const RuntimeVoice &a,const RuntimeVoice &b){return a.id<b.id;});
    return out;
}

inline std::optional<RuntimeVoice> runtime_voice(bool chatterbox, unsigned index) {
    const auto voices=runtime_voices(chatterbox);
    if (voices.empty()) return std::nullopt;
    return voices[index<voices.size() ? index : 0];
}
inline int runtime_voice_index(bool chatterbox, const std::string &id) {
    const auto voices=runtime_voices(chatterbox);
    for (std::size_t i=0;i<voices.size();++i) if (voices[i].id==id) return static_cast<int>(i);
    return -1;
}
inline bool delete_chatterbox_voice(const std::string &id) {
    for (const auto &voice : runtime_voices(true)) {
        if (voice.id!=id || !chatterbox_voice_deletable(voice)) continue;
        const auto path=runtime_voice_directory(true)+"/"+voice.filename;
        struct stat info{};
        return lstat(path.c_str(),&info)==0 && S_ISREG(info.st_mode) && std::remove(path.c_str())==0;
    }
    return false;
}
inline RuntimeVoice runtime_voice_by_id(bool chatterbox, const std::string &id) {
    const auto voices=runtime_voices(chatterbox);
    for (const auto &v : voices) if (v.id==id) return v;
    if (!voices.empty()) return voices.front();
    return {};
}
inline std::vector<RuntimeVoice> chatterbox_catalog() {
    std::vector<RuntimeVoice> out;
    for (std::size_t i=0;i<chatterbox_count;++i) { const auto &v=chatterbox[i]; out.push_back({v.id,v.name,v.locale,v.filename}); }
    return out;
}
} // namespace prospero_voice
