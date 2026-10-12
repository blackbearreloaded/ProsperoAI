// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_vulkan.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    assert(prospero_media_vk_reserve() == 0);
    assert(prospero_media_vk::matmul(nullptr, nullptr, nullptr, 0, 0, 0) != 0);

    // Contiguous row-major A[2,3] x B[3,2] -> C[2,2].
    const float a[] = {1, 2, 3, 4, 5, 6};
    const float b[] = {7, 8, 9, 10, 11, 12};
    float product[4]{};
    assert(prospero_media_vk::matmul(a, b, product, 2, 2, 3) == 0);
    const float expected[] = {58, 64, 139, 154};
    for (int i = 0; i < 4; ++i)
        assert(std::fabs(product[i] - expected[i]) < 1e-4f);

    const float input[] = {-2, -1, 0, 1, 2, 3};
    float output[6]{};
    assert(prospero_media_vk::unary(prospero_media_vk::Unary::Silu, input, output, 2, 3) == 0);
    for (int i = 0; i < 6; ++i)
    {
        const float reference = input[i] / (1.0f + std::exp(-input[i]));
        assert(std::fabs(output[i] - reference) < 1e-4f);
    }
    prospero_media_vk::release();
    std::puts("PASS ggml/Vulkan media bridge matmul and SiLU parity");
}
