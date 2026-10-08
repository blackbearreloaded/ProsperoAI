// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace prospero_model_download {
enum class State { Idle, Loading, Ready, Downloading, Complete, Failed };
struct Candidate { char name[160]; std::uint64_t size; };
void poll();
State state();
void status(char *output, std::size_t capacity);
std::size_t candidate_count();
bool candidate(std::size_t index, Candidate *output);
bool browse(const char *repository);
bool download(std::size_t index);
}
