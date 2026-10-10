// SPDX-License-Identifier: GPL-3.0-or-later
#include "kv_budget.hpp"
#include <algorithm>
#include <array>

namespace prospero_kv {
namespace {
unsigned type_bytes(KvType type) {
    switch (type) {
    case KvType::F16: return 2;
    case KvType::Q8_0: return 1;
    case KvType::Q4_0: return 1; // packed below a byte; the per-token helper rounds up.
    }
    return 2;
}
}

std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type) {
    // K and V, one cache each, type_bytes() bytes per element (q4_0's 4 bits/element is
    // approximated as 1 byte here as a conservative upper bound; good enough for admission).
    return static_cast<std::uint64_t>(shape.kv_layers) * shape.kv_heads * shape.head_dim * 2 *
           type_bytes(type);
}

KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                      std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes) {
    constexpr std::array<unsigned, 6> steps{4096, 8192, 16384, 32768, 65536, 131072};
    // A model trained at a context shorter than the smallest step (e.g. 2048) has nothing in
    // `steps` to clamp to; return its own ceiling directly rather than falling through to the
    // loop below, which would otherwise return the unclamped steps[0].
    if (shape.n_ctx_train < steps[0]) return {shape.n_ctx_train, KvType::F16, false};
    const std::uint64_t usable =
        free_bytes > weights_bytes + compute_reserve_bytes
            ? free_bytes - weights_bytes - compute_reserve_bytes
            : 0;
    KvChoice best{steps[0], KvType::F16, false};
    for (unsigned step : steps) {
        if (step > shape.n_ctx_train) break;
        const KvType type = step <= 8192 ? KvType::F16 : KvType::Q8_0;
        const std::uint64_t needed =
            static_cast<std::uint64_t>(step) * kv_bytes_per_token(shape, type);
        if (needed > usable && step != steps[0]) break; // keep the previous (smaller) fit
        best = {step, type, type != KvType::F16};
    }
    return best;
}
}
