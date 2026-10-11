// SPDX-License-Identifier: GPL-3.0-or-later
#include "kv_budget.hpp"
#include <algorithm>
#include <array>

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

KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                     std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes,
                     const KvType *fixed_type)
{
    constexpr std::array<unsigned, 6> steps{4096, 8192, 16384, 32768, 65536, 131072};
    // A model trained at a context shorter than the smallest step (e.g. 2048) has nothing in
    // `steps` to clamp to; return its own ceiling directly rather than falling through to the
    // loop below, which would otherwise return the unclamped steps[0].
    if (shape.n_ctx_train < steps[0])
        return {shape.n_ctx_train, fixed_type ? *fixed_type : KvType::F16,
                fixed_type && *fixed_type != KvType::F16};
    const std::uint64_t usable =
        free_bytes > weights_bytes && free_bytes - weights_bytes > compute_reserve_bytes
            ? free_bytes - weights_bytes - compute_reserve_bytes
            : 0;
    KvChoice best{steps[0], fixed_type ? *fixed_type : KvType::F16,
                  fixed_type && *fixed_type != KvType::F16};
    for (unsigned step : steps)
    {
        if (step > shape.n_ctx_train)
            break;
        const KvType type = fixed_type ? *fixed_type : step <= 8192 ? KvType::F16 : KvType::Q8_0;
        const std::uint64_t needed =
            static_cast<std::uint64_t>(step) * kv_bytes_per_token(shape, type);
        if (needed > usable)
            continue; // q8_0 at 16K can fit even when f16 at 8K does not.
        best = {step, type, type != KvType::F16};
    }
    return best;
}
} // namespace prospero_kv
