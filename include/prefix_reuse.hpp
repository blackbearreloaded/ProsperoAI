// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>

namespace prospero_kv {
std::size_t common_prefix_length(const std::int32_t *old_tokens, std::size_t old_count,
                                  const std::int32_t *new_tokens, std::size_t new_count);
}
