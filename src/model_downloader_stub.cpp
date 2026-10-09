// SPDX-License-Identifier: GPL-3.0-or-later
// The AGC build shares the native UI but does not include the Vulkan downloader.
#include "model_downloader_ps5.hpp"

#include <cstdio>

namespace prospero_model_download
{
void poll()
{
}
State state()
{
    return State::Idle;
}
void status(char *output, std::size_t capacity)
{
    if (output && capacity)
        std::snprintf(output, capacity, "%s", "Model downloads require the Vulkan build.");
}
std::size_t candidate_count()
{
    return 0;
}
bool candidate(std::size_t, Candidate *)
{
    return false;
}
bool search(const char *)
{
    return false;
}
bool browse(const char *)
{
    return false;
}
bool download(std::size_t)
{
    return false;
}
} // namespace prospero_model_download
