// SPDX-License-Identifier: GPL-3.0-or-later
#include "../include/prefix_reuse.hpp"
#include <cassert>

int main()
{
    using prospero_kv::common_prefix_length;
    const std::int32_t a[] = {1, 2, 3, 4};
    const std::int32_t b[] = {1, 2, 3, 4, 5};
    assert(common_prefix_length(a, 4, b, 5) == 4);       // b just grew: whole old prefix reused
    assert(common_prefix_length(a, 4, a, 4) == 4);       // identical: fully reused
    assert(common_prefix_length(nullptr, 0, b, 5) == 0); // nothing to reuse yet
    const std::int32_t c[] = {1, 2, 9, 4};
    assert(common_prefix_length(a, 4, c, 4) == 2); // diverges at index 2
    const std::int32_t d[] = {9, 2, 3, 4};
    assert(common_prefix_length(a, 4, d, 4) == 0); // diverges at index 0: full reprefill
    assert(common_prefix_length(a, 4, b, 2) == 2); // shorter new sequence caps the result
}
