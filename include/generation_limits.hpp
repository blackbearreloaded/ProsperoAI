// ProsperoAI text-generation limits shared by the runtime, APIs and interfaces.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace prospero_generation
{
constexpr unsigned MaxOutputTokens = 131072;
constexpr unsigned MaxContextTokens = 262144;
constexpr std::size_t MaxOutputBytes = 2 * 1024 * 1024;

inline bool valid_output_limit(unsigned value)
{
    return value == 0 || value == 64 || value == 128 || value == 192 || value == 256 ||
           value == 512 || value == 1024 || value == 2048 || value == 4096 || value == 8192 ||
           value == 16384 || value == 32768 || value == 65536 || value == 131072;
}
} // namespace prospero_generation
