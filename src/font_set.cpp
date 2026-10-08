// ProsperoAI - The faces the interface draws with, and the ones that stand behind them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "font_set.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"

namespace prospero
{
namespace
{
constexpr const char *kFiles[] = {"inter-regular.huifont",        "inter-semibold.huifont",
                                  "montserrat-medium.huifont",    "dejavu-sans-mono.huifont",
                                  "noto-sans-east-asian.huifont", "noto-sans-korean.huifont",
                                  "legacy-multilingual.huifont"};
// The largest atlas (Chinese and Japanese) is about 26 MB.
constexpr std::size_t kLargestFont = 64u << 20;

bool is_hangul(std::uint32_t c)
{
    return (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3130 && c <= 0x318f) ||
           (c >= 0xac00 && c <= 0xd7a3);
}
bool is_east_asian(std::uint32_t c)
{
    return (c >= 0x2e80 && c <= 0x312f) || (c >= 0x31a0 && c <= 0x4dbf) ||
           (c >= 0x4e00 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) ||
           (c >= 0xff00 && c <= 0xffef);
}
} // namespace

bool FontSet::open(hui::gfx::Renderer &renderer, std::string directory)
{
    renderer_ = &renderer;
    directory_ = std::move(directory);
    for (const Face face : {regular, semibold, display, mono})
        if (!load(face))
            return false;
    fonts_.regular = {&faces_[regular], textures_[regular]};
    fonts_.semibold = {&faces_[semibold], textures_[semibold]};
    fonts_.display = {&faces_[display], textures_[display]};
    fonts_.mono = {&faces_[mono], textures_[mono]};
    // The kit's Pixel and Sketch themes are not used here.
    fonts_.pixel = fonts_.hand = fonts_.regular;
    return true;
}

bool FontSet::load(Face face)
{
    std::string data;
    if (!hui::save::read_file(directory_ + "/" + kFiles[face], &data, kLargestFont) ||
        !faces_[face].load(data))
    {
        hui::sys::log("[prosperoai] font load failed: %s", kFiles[face]);
        failed_[face] = true;
        return false;
    }
    textures_[face] = renderer_->batch().create_font_texture(faces_[face]);
    loaded_[face] = textures_[face] != 0;
    failed_[face] = !loaded_[face];
    return loaded_[face];
}

void FontSet::link()
{
    for (const Face face : {regular, semibold, display, mono})
    {
        faces_[face].clear_fallbacks();
        for (const Face behind : {east_asian, korean, legacy})
            if (loaded_[behind])
                faces_[face].add_fallback(&faces_[behind], textures_[behind]);
    }
}

void FontSet::note(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();)
    {
        if (static_cast<unsigned char>(text[index]) < 0x80)
        {
            ++index;
            continue;
        }
        const std::uint32_t c = hui::gfx::next_codepoint(text, &index);
        if (is_hangul(c))
            wanted_[korean] = true;
        else if (is_east_asian(c))
            wanted_[east_asian] = true;
        else if (!faces_[regular].has_glyph(c))
            wanted_[legacy] = true;
    }
}

bool FontSet::load_pending()
{
    bool changed = false;
    for (const Face face : {east_asian, korean, legacy})
    {
        if (!wanted_[face] || loaded_[face] || failed_[face])
            continue;
        const auto began = hui::sys::monotonic_us();
        if (load(face))
        {
            changed = true;
            hui::sys::log("[prosperoai] %s loaded in %lld ms", kFiles[face],
                          static_cast<long long>((hui::sys::monotonic_us() - began) / 1000));
        }
    }
    if (changed)
    {
        link();
        ++revision_;
    }
    return changed;
}
} // namespace prospero
