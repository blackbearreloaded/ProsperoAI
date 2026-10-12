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
// Per-kernel accounting for adapters which need to distinguish Vulkan dispatch and CPU fallback.
void begin_kernel();
void end_kernel(const char *name, unsigned long long microseconds);
// Host-side work that is not a kernel at all: buffer creation, uploads, readbacks, conversions.
void record_host(const char *name, unsigned long long microseconds);
// Where print_stats() writes. Without a sink it writes to stderr, which PS5 titles drop.
void set_log_sink(void (*sink)(const char *));
void print_stats();
void release();
}
extern "C" int prospero_media_vk_reserve();
