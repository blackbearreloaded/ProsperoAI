// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <sys/time.h>

inline bool prospero_generated_output_path(char *output, std::size_t capacity,
                                          const char *modality, const char *stem,
                                          const char *extension)
{
    if (!output || capacity == 0 || !modality || !stem || !extension)
        return false;
    timeval now{};
    if (gettimeofday(&now, nullptr) != 0)
        return false;
    const std::uint64_t timestamp = static_cast<std::uint64_t>(now.tv_sec) * 1000000ULL +
                                   static_cast<std::uint64_t>(now.tv_usec);
    static std::atomic<std::uint32_t> serial{0};
    for (unsigned attempt = 0; attempt < 64; ++attempt)
    {
        const auto number = serial.fetch_add(1, std::memory_order_relaxed);
        const int length = std::snprintf(output, capacity,
                                         "/download0/ProsperoAI/work/%s/%s-%llu-%u.%s",
                                         modality, stem, static_cast<unsigned long long>(timestamp),
                                         number, extension);
        if (length < 0 || static_cast<std::size_t>(length) >= capacity)
            return false;
        struct stat info
        {
        };
        if (stat(output, &info) != 0 && errno == ENOENT)
            return true;
    }
    return false;
}
