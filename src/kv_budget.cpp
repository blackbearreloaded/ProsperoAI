// SPDX-License-Identifier: GPL-3.0-or-later
#include "kv_budget.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace prospero_kv
{
std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type)
{
    // ggml cache rows are block-quantized: q8_0 = 34 bytes/32, q4_0 = 18 bytes/32.
    const std::uint64_t row = type == KvType::F16 ? std::uint64_t(shape.head_dim) * 2
                                                  : ((std::uint64_t(shape.head_dim) + 31) / 32) *
                                                        (type == KvType::Q8_0 ? 34 : 18);
    return std::uint64_t(shape.kv_layers) * shape.kv_heads * 2 * row;
}

std::uint64_t kv_bytes_for_context(const ModelKvShape &shape, KvType type,
                                   unsigned context_length)
{
    const auto per_token = kv_bytes_per_token(shape, type);
    if (per_token && context_length > std::numeric_limits<std::uint64_t>::max() / per_token)
        return std::numeric_limits<std::uint64_t>::max();
    return per_token * context_length;
}

KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                     std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes,
                     const KvType *fixed_type)
{
    constexpr std::array<unsigned, 7> steps{4096, 8192, 16384, 32768, 65536, 131072, 262144};
    // A model trained at a context shorter than the smallest step (e.g. 2048) has nothing in
    // `steps` to clamp to; return its own ceiling directly rather than falling through to the
    // loop below, which would otherwise return the unclamped steps[0].
    if (shape.n_ctx_train < steps[0])
    {
        const KvType type = fixed_type ? *fixed_type : KvType::F16;
        const auto required = kv_bytes_for_context(shape, type, shape.n_ctx_train);
        const auto usable = free_bytes > weights_bytes && free_bytes - weights_bytes > compute_reserve_bytes
                                ? free_bytes - weights_bytes - compute_reserve_bytes
                                : 0;
        if (!required || required > usable)
            return {0, type, type != KvType::F16};
        return {shape.n_ctx_train, type, type != KvType::F16};
    }
    const std::uint64_t usable =
        free_bytes > weights_bytes && free_bytes - weights_bytes > compute_reserve_bytes
            ? free_bytes - weights_bytes - compute_reserve_bytes
            : 0;
    KvChoice best{0, fixed_type ? *fixed_type : KvType::F16,
                  fixed_type && *fixed_type != KvType::F16};
    for (unsigned step : steps)
    {
        if (step > shape.n_ctx_train)
            break;
        const KvType preferred = fixed_type ? *fixed_type : step <= 8192 ? KvType::F16 : KvType::Q8_0;
        const std::uint64_t needed = kv_bytes_for_context(shape, preferred, step);
        if (needed <= usable) {
            best = {step, preferred, preferred != KvType::F16};
            continue;
        }
        // On constrained GPUs, retain the largest usable context by trying Q8 KV at
        // smaller steps too. This is particularly useful for large hybrid models
        // whose weight buffers leave less room than the usual F16 4K cache.
        if (!fixed_type && preferred == KvType::F16) {
            const auto q8_needed = kv_bytes_for_context(shape, KvType::Q8_0, step);
            if (q8_needed <= usable)
                best = {step, KvType::Q8_0, true};
        }
    }
    return best;
}
} // namespace prospero_kv
