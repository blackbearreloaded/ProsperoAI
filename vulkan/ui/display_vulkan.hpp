// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "backend.hpp"
namespace hui::ps5
{
class Display
{
  public:
    bool open(int width = 1920, int height = 1080)
    {
        return prospero::vkui::open(true, width, height);
    }
    bool swap()
    {
        return prospero::vkui::swap();
    }
    void close()
    {
        prospero::vkui::close();
    }
    static bool supports_display_modes()
    {
        return false;
    }
    int width() const
    {
        return prospero::vkui::width();
    }
    int height() const
    {
        return prospero::vkui::height();
    }
};
} // namespace hui::ps5
