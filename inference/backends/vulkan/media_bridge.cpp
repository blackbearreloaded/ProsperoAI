// SPDX-License-Identifier: GPL-3.0-or-later
// Bridge the existing audio tensor layouts to the same ggml Vulkan ABI as text.
#include "media_vulkan.hpp"
#include "ggml.h"
#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-vulkan.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace {
std::mutex mutex;
ggml_backend_t backend;
unsigned calls, cpu_calls;
std::uint64_t elapsed_us;
void (*log_sink)(const char *);
thread_local bool kernel_on_cpu;
struct KernelCost {
    unsigned cpu_calls, gpu_calls;
    std::uint64_t cpu_us, gpu_us;
};
std::map<std::string, KernelCost> kernels;
void emit(const char *line) {
    if (log_sink) log_sink(line);
    else std::fprintf(stderr, "%s", line);
}
using Context = std::unique_ptr<ggml_context, decltype(&ggml_free)>;
using Buffer = std::unique_ptr<ggml_backend_buffer, decltype(&ggml_backend_buffer_free)>;
bool shape(std::size_t rows, std::size_t columns) {
    return rows && columns && rows <= static_cast<std::size_t>(INT64_MAX) &&
           columns <= static_cast<std::size_t>(INT64_MAX) &&
           rows <= std::numeric_limits<std::size_t>::max() / sizeof(float) / columns;
}
bool init() {
    if (!backend) {
        if (ggml_backend_vk_get_device_count() == 0) return false;
        backend = ggml_backend_vk_init(0);
    }
    return backend != nullptr;
}
Context context() {
    return Context(ggml_init({ggml_tensor_overhead() * 16 + ggml_graph_overhead_custom(16, false),
                             nullptr, true}), ggml_free);
}
int compute(ggml_cgraph *graph, ggml_tensor *result, float *out) {
    if (!ggml_backend_supports_op(backend, result)) return -1;
    if (ggml_backend_graph_compute(backend, graph) != GGML_STATUS_SUCCESS) return -1;
    ggml_backend_tensor_get(result, out, 0, ggml_nbytes(result));
    ++calls;
    return 0;
}
struct Timer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~Timer() { elapsed_us += std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - start).count(); }
};
}
extern "C" int prospero_media_vk_reserve() {
    std::lock_guard<std::mutex> lock(mutex);
    try { return init() ? 0 : -1; } catch (...) { return -1; }
}
namespace prospero_media_vk {
int matmul(const float *a, const float *b, float *out, std::size_t m,
           std::size_t n, std::size_t k) {
    if (!a || !b || !out || !shape(m,k) || !shape(n,k) || !shape(m,n)) return -1;
    std::lock_guard<std::mutex> lock(mutex);
    Timer timer;
    try {
        if (!init()) return -1;
        auto ctx = context();
        if (!ctx) return -1;
        auto *left = ggml_new_tensor_2d(ctx.get(), GGML_TYPE_F32, k, n);
        auto *right = ggml_new_tensor_2d(ctx.get(), GGML_TYPE_F32, k, m);
        auto *result = ggml_mul_mat(ctx.get(), left, right);
        ggml_prec_set_acc(result, GGML_PREC_F32);
        ggml_set_input(left); ggml_set_input(right); ggml_set_output(result);
        auto *graph = ggml_new_graph_custom(ctx.get(), 16, false);
        ggml_build_forward_expand(graph, result);
        if (!ggml_backend_supports_op(backend, result)) return -1;
        Buffer buffer(ggml_backend_alloc_ctx_tensors(ctx.get(), backend), ggml_backend_buffer_free);
        if (!buffer) return -1;
        // ggml stores the first tensor dimension contiguously; the public input
        // uses conventional row-major B[K,N], so transpose it to [N,K] storage.
        std::vector<float> b_transposed(k * n);
        for (std::size_t column = 0; column < n; ++column)
            for (std::size_t inner = 0; inner < k; ++inner)
                b_transposed[column * k + inner] = b[inner * n + column];
        ggml_backend_tensor_set(left, b_transposed.data(), 0, ggml_nbytes(left));
        ggml_backend_tensor_set(right, a, 0, ggml_nbytes(right));
        return compute(graph, result, out);
    } catch (...) { return -1; }
}
int unary(Unary op, const float *in, float *out, std::size_t rows,
          std::size_t columns, float epsilon) {
    if (!in || !out || !shape(rows,columns)) return -1;
    std::lock_guard<std::mutex> lock(mutex);
    Timer timer;
    try {
        if (!init()) return -1;
        auto ctx = context();
        if (!ctx) return -1;
        auto *input = ggml_new_tensor_2d(ctx.get(), GGML_TYPE_F32, columns, rows);
        ggml_tensor *result = nullptr;
        switch (op) {
        case Unary::Silu: result = ggml_silu(ctx.get(), input); break;
        case Unary::Gelu: result = ggml_gelu(ctx.get(), input); break;
        case Unary::Softmax: result = ggml_soft_max(ctx.get(), input); break;
        case Unary::Norm: result = ggml_norm(ctx.get(), input, epsilon); break;
        }
        if (!result || !ggml_backend_supports_op(backend, result)) return -1;
        ggml_set_input(input); ggml_set_output(result);
        auto *graph = ggml_new_graph_custom(ctx.get(), 16, false);
        ggml_build_forward_expand(graph, result);
        Buffer buffer(ggml_backend_alloc_ctx_tensors(ctx.get(), backend), ggml_backend_buffer_free);
        if (!buffer) return -1;
        ggml_backend_tensor_set(input, in, 0, ggml_nbytes(input));
        return compute(graph, result, out);
    } catch (...) { return -1; }
}
void record_cpu_fallback() {
    kernel_on_cpu = true;
    std::lock_guard<std::mutex> lock(mutex);
    ++cpu_calls;
}
void begin_kernel() { kernel_on_cpu = false; }
void record_host(const char *name, unsigned long long microseconds) {
    if (!name) return;
    std::lock_guard<std::mutex> lock(mutex);
    auto &cost = kernels[name];
    ++cost.cpu_calls;
    cost.cpu_us += microseconds;
}
void end_kernel(const char *name, unsigned long long microseconds) {
    if (!name) return;
    const bool on_cpu = kernel_on_cpu;
    std::lock_guard<std::mutex> lock(mutex);
    auto &cost = kernels[name];
    if (on_cpu) { ++cost.cpu_calls; cost.cpu_us += microseconds; }
    else { ++cost.gpu_calls; cost.gpu_us += microseconds; }
}
void set_log_sink(void (*sink)(const char *)) {
    std::lock_guard<std::mutex> lock(mutex);
    log_sink = sink;
}
void print_stats() {
    std::lock_guard<std::mutex> lock(mutex);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "media-vulkan calls=%u cpu_kernel_calls=%u bridge_us=%llu backend=Vulkan\n",
                  calls, cpu_calls, static_cast<unsigned long long>(elapsed_us));
    emit(line);
    // The expensive kernels decide what is worth porting next, so name them with their
    // own measured wall time instead of only counting the fallbacks.
    std::vector<std::pair<std::string, KernelCost>> sorted(kernels.begin(), kernels.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        return a.second.cpu_us + a.second.gpu_us > b.second.cpu_us + b.second.gpu_us;
    });
    std::uint64_t total_cpu = 0, total_gpu = 0;
    for (const auto &entry : sorted) {
        total_cpu += entry.second.cpu_us;
        total_gpu += entry.second.gpu_us;
    }
    std::snprintf(line, sizeof(line), "media-vulkan kernel totals cpu_us=%llu gpu_us=%llu kinds=%zu\n",
                  static_cast<unsigned long long>(total_cpu),
                  static_cast<unsigned long long>(total_gpu), sorted.size());
    emit(line);
    unsigned shown = 0;
    for (const auto &entry : sorted) {
        if (shown++ >= 16) break;
        std::snprintf(line, sizeof(line),
                      "media-vulkan kernel %-28s cpu=%ux/%llu us gpu=%ux/%llu us\n",
                      entry.first.c_str(), entry.second.cpu_calls,
                      static_cast<unsigned long long>(entry.second.cpu_us), entry.second.gpu_calls,
                      static_cast<unsigned long long>(entry.second.gpu_us));
        emit(line);
    }
}
void release() {
    std::lock_guard<std::mutex> lock(mutex);
    if (backend) ggml_backend_free(backend);
    backend = nullptr;
    calls = 0; cpu_calls = 0; elapsed_us = 0;
    kernels.clear();
}
}
