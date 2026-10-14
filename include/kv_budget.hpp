// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace prospero_kv {
constexpr std::uint64_t validated_free_bytes(std::uint64_t free_bytes, std::uint64_t total_bytes) {
    return total_bytes && free_bytes <= total_bytes ? free_bytes : 0;
}
enum class KvType { F16, Q8_0, Q4_0 };
struct ModelKvShape {
    unsigned kv_layers;
    unsigned kv_heads;
    unsigned head_dim;
    unsigned n_ctx_train;
};
struct KvChoice {
    unsigned context_length;
    KvType kv_type;
    bool flash_attention;
};
std::uint64_t kv_bytes_per_token(const ModelKvShape &shape, KvType type);
std::uint64_t kv_bytes_for_context(const ModelKvShape &shape, KvType type,
                                   unsigned context_length);
KvChoice choose_auto(const ModelKvShape &shape, std::uint64_t weights_bytes,
                      std::uint64_t compute_reserve_bytes, std::uint64_t free_bytes,
                      const KvType *fixed_type = nullptr);
}
