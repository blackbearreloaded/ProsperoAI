// SPDX-License-Identifier: GPL-3.0-or-later
#include "../include/kv_budget.hpp"
#include <cassert>

int main()
{
    using namespace prospero_kv;
    // Mistral 7B v0.3 shape from docs/PLAN_CONTEXT_MODELS_VULKAN.md: 32 layers, 8 KV heads x 128
    // dim.
    ModelKvShape mistral{32, 8, 128, 32768};
    // RADV reported wrapped unsigned free bytes during Bonsai context recreation.
    const auto invalid_free = validated_free_bytes(18446744070348734464ULL, 12280922112ULL);
    assert(invalid_free == 0);
    assert(choose_auto(mistral, 0, 0, invalid_free).context_length == 0);
    assert(validated_free_bytes(4096, 8192) == 4096);
    assert(validated_free_bytes(4096, 0) == 0);
    assert(kv_bytes_per_token(mistral, KvType::F16) ==
           32ULL * 8 * 128 * 2 * 2); // layers * heads * dim * (K+V) * 2 bytes
    assert(kv_bytes_per_token(mistral, KvType::Q8_0) == 32ULL * 8 * 2 * 4 * 34);
    assert(kv_bytes_per_token(mistral, KvType::Q4_0) == 32ULL * 8 * 2 * 4 * 18);
    assert(kv_bytes_for_context(mistral, KvType::F16, 4096) ==
           kv_bytes_per_token(mistral, KvType::F16) * 4096);
    assert(kv_bytes_for_context(mistral, KvType::F16, ~0u) >
           kv_bytes_for_context(mistral, KvType::F16, 4096));
    // Plenty of free memory: picks the largest step <= n_ctx_train at f16 up to 8K, q8_0 above.
    {
        auto choice = choose_auto(mistral, /*weights=*/4ULL * 1024 * 1024 * 1024,
                                  /*compute reserve=*/512ULL * 1024 * 1024,
                                  /*free=*/11ULL * 1024 * 1024 * 1024);
        assert(choice.context_length <= mistral.n_ctx_train);
        assert(choice.context_length >= 4096);
        if (choice.context_length > 8192)
        {
            assert(choice.kv_type == KvType::Q8_0);
            assert(choice.flash_attention);
        }
        else
        {
            assert(choice.kv_type == KvType::F16);
            assert(!choice.flash_attention);
        }
    }
    // A model whose n_ctx_train is small clamps rather than overshoots it.
    {
        ModelKvShape tiny{4, 2, 64, 2048};
        auto choice =
            choose_auto(tiny, 100ULL * 1024 * 1024, 64ULL * 1024 * 1024, 8ULL * 1024 * 1024 * 1024);
        assert(choice.context_length == 2048);
    }
    // Memory too tight even for the smallest step refuses before KV allocation.
    {
        auto choice = choose_auto(mistral, 4ULL * 1024 * 1024 * 1024, 512ULL * 1024 * 1024,
                                  /*free=*/4ULL * 1024 * 1024 * 1024 + 600ULL * 1024 * 1024);
        assert(choice.context_length == 0);
    }
    // Subtraction must not wrap when weights plus reserve exceed uint64_t.
    assert(choose_auto(mistral, ~0ULL, 1024, ~0ULL).context_length == 0);
    // A constrained heap can still fit the minimum context using quantized KV,
    // rather than rejecting a model whose weights already occupy most of VRAM.
    {
        const auto f16_min = kv_bytes_for_context(mistral, KvType::F16, 4096);
        const auto q8_min = kv_bytes_for_context(mistral, KvType::Q8_0, 4096);
        const auto constrained = choose_auto(mistral, 0, 0, q8_min + 1, nullptr);
        assert(constrained.context_length == 4096);
        assert(constrained.kv_type == KvType::Q8_0);
        assert(constrained.flash_attention);
        assert(q8_min < f16_min);
    }
    const KvType f16 = KvType::F16;
    const auto fixed = choose_auto(mistral, 0, 0, 3ULL * 1024 * 1024 * 1024, &f16);
    assert(fixed.context_length == 16384 && fixed.kv_type == KvType::F16 && !fixed.flash_attention);
    // Every quantized choice pairs with flash attention, as the spec requires.
    for (unsigned free_gib = 4; free_gib <= 16; ++free_gib)
    {
        auto choice = choose_auto(mistral, 4ULL * 1024 * 1024 * 1024, 512ULL * 1024 * 1024,
                                  static_cast<std::uint64_t>(free_gib) * 1024 * 1024 * 1024);
        assert(choice.kv_type == KvType::F16 || choice.flash_attention);
    }
}
