// SPDX-License-Identifier: GPL-3.0-or-later
#include "prefix_reuse.hpp"
#include <algorithm>

namespace prospero_kv
{
std::size_t common_prefix_length(const std::int32_t *old_tokens, std::size_t old_count,
                                 const std::int32_t *new_tokens, std::size_t new_count)
{
    const std::size_t limit = std::min(old_count, new_count);
    std::size_t i = 0;
    while (i < limit && old_tokens && new_tokens && old_tokens[i] == new_tokens[i])
        ++i;
    return i;
}
} // namespace prospero_kv
