// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "model_presets.hpp"
#include <cstddef>
#include <cstdint>

namespace prospero_model_download {
enum class State { Idle, Searching, SearchReady, Loading, Ready, Downloading, Complete, Failed };
struct Candidate { char name[160]; std::uint64_t size; };
bool preset_installed(std::size_t index);
bool download_preset(std::size_t index);
void poll();
State state();
int active_preset();
void progress(std::uint64_t *completed, std::uint64_t *total);
void status(char *output, std::size_t capacity);
std::size_t candidate_count();
bool candidate(std::size_t index, Candidate *output);
bool search(const char *query);
bool browse(const char *repository);
bool download(std::size_t index);
}
