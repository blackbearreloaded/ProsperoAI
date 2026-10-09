// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace prospero::vkui
{
// Main-thread UI backend. Inference owns its separate Vulkan device/queue.
// No GL/EGL driver is used: the narrow upstream draw API is translated to Vulkan.
bool open(bool presentation, int width = 1920, int height = 1080);
bool swap();
void close();
int width();
int height();
std::uint32_t program(const char *name);
} // namespace prospero::vkui
