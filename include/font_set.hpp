// ProsperoAI - The faces the interface draws with, and the ones that stand behind them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gfx/font.hpp"
#include "gfx/renderer.hpp"
#include "ui/fonts.hpp"
#include <array>
#include <string>
#include <string_view>

namespace prospero
{
// Four faces draw the interface (Latin, Greek and Cyrillic). A model answers in
// whatever language it was asked in, so three more stand behind them as
// fallbacks, and each is read from disk the first time a text needs it:
// Chinese and Japanese, Korean, and a small last-resort face for other scripts.
class FontSet
{
  public:
    // Reads the four faces of the interface from `directory` (no trailing slash).
    bool open(hui::gfx::Renderer &renderer, std::string directory);
    const hui::ui::Fonts &fonts() const
    {
        return fonts_;
    }
    // Looks at the code points of a text the screen is about to show and
    // remembers the fallback faces it calls for.
    void note(std::string_view text);
    // Reads the faces noted since the last call: a file and a texture each,
    // so the frame it happens on is a long one. True when the set changed and
    // text measured before must be measured again.
    bool load_pending();
    // How many times the set changed: a cheap "measure again" signal.
    unsigned revision() const
    {
        return revision_;
    }

  private:
    enum Face : unsigned
    {
        regular,
        semibold,
        display,
        mono,
        east_asian,
        korean,
        legacy,
        count
    };
    bool load(Face face);
    void link();

    hui::gfx::Renderer *renderer_ = nullptr;
    std::string directory_;
    std::array<hui::gfx::Font, count> faces_;
    std::array<std::uint32_t, count> textures_{};
    std::array<bool, count> loaded_{};
    std::array<bool, count> wanted_{};
    std::array<bool, count> failed_{};
    hui::ui::Fonts fonts_;
    unsigned revision_ = 0;
};
} // namespace prospero
