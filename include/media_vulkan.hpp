// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
namespace prospero_media_vk {
enum class Unary { Silu, Gelu, Softmax, Norm };
// Contiguous row-major A[m,k], B[n,k], output[m,n]. No implicit CPU fallback.
int matmul(const float *a, const float *b, float *out, std::size_t m,
           std::size_t n, std::size_t k);
int unary(Unary op, const float *in, float *out, std::size_t rows,
          std::size_t columns, float epsilon = 0);
void record_cpu_fallback();
void print_stats();
void release();
}
extern "C" int prospero_media_vk_reserve();
