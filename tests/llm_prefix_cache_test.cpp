// Exercise the actual Vulkan runtime with llama.cpp CPU inference on the host.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "llama.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
static bool fail_next_decode = false;
static unsigned evaluated_tokens = 0;
static bool reject_tail_removal = false;
static bool recurrent_fixture = false;
static bool hybrid_fixture = false;
static bool missing_cache_start = false;
static bool chat_fixture = false;
static const char *runtime_test_template(const llama_model *m, const char *name)
{
    return chat_fixture ? "chatml" : llama_model_chat_template(m, name);
}
static bool runtime_test_rm(llama_memory_t mem, llama_seq_id seq, llama_pos lo, llama_pos hi)
{
    return !reject_tail_removal && llama_memory_seq_rm(mem, seq, lo, hi);
}
static bool runtime_test_recurrent(const llama_model *m)
{
    return recurrent_fixture || llama_model_is_recurrent(m);
}
static bool runtime_test_hybrid(const llama_model *m)
{
    return hybrid_fixture || llama_model_is_hybrid(m);
}
static llama_pos runtime_test_pos_min(llama_memory_t mem, llama_seq_id seq)
{
    return missing_cache_start ? 1 : llama_memory_seq_pos_min(mem, seq);
}
static int32_t runtime_test_decode(llama_context *ctx, llama_batch batch)
{
    const int32_t result = llama_decode(ctx, batch);
    evaluated_tokens += batch.n_tokens;
    // Simulate an error after a batch mutated memory, as an aborted ubatch can.
    if (fail_next_decode)
    {
        fail_next_decode = false;
        return 2;
    }
    return result;
}
#define PS5_LLAMA_VULKAN
#define llama_decode runtime_test_decode
#define llama_memory_seq_rm runtime_test_rm
#define llama_model_is_recurrent runtime_test_recurrent
#define llama_model_is_hybrid runtime_test_hybrid
#define llama_memory_seq_pos_min runtime_test_pos_min
#define llama_model_chat_template runtime_test_template
#include "../vulkan/gpt_runtime_vulkan.cpp"
#undef llama_decode
#undef llama_memory_seq_rm
#undef llama_model_is_recurrent
#undef llama_model_is_hybrid
#undef llama_memory_seq_pos_min
#undef llama_model_chat_template
extern "C" int sceKernelDebugOutText(int, const char *text)
{
    std::fputs(text, stderr);
    return 0;
}

static std::string model_path;
static void load_fixture()
{
    release_model();
    auto mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    mp.load_mode = LLAMA_LOAD_MODE_NONE;
    model = llama_model_load_from_file(model_path.c_str(), mp);
    assert(model);
    auto cp = llama_context_default_params();
    cp.n_ctx = 512;
    cp.n_batch = 32;
    cp.n_ubatch = 32;
    cp.n_threads = 2;
    cp.n_threads_batch = 2;
    cp.no_perf = false;
    context = llama_init_from_model(model, cp);
    assert(context);
    scanned = true;
    const auto name = model_path.substr(model_path.find_last_of('/') + 1);
    files = {{name, model_path}, {"other.gguf", model_path}};
    selected = 0;
}
static std::string generate(const std::string &prompt, gpt_runtime_stats_t &stats,
                            unsigned limit = 12, std::size_t capacity = 4096,
                            const char *grammar = nullptr)
{
    gpt_runtime_message_t message{"user", prompt.c_str()};
    gpt_runtime_settings_t settings{};
    settings.max_output_tokens = limit;
    settings.grammar = grammar;
    char output[4096];
    const int rc = gpt_runtime_generate(&message, 1, settings, output, capacity, &stats, nullptr);
    assert(rc == 0);
    return output;
}
static void quiet_log(ggml_log_level, const char *, void *)
{
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    model_path = argv[1];
    llama_log_set(quiet_log, nullptr);
    ggml_log_set(quiet_log, nullptr);
    llama_backend_init();
    load_fixture();
    const std::string base = "Once upon a time there was a little girl who lived in a village.";
    gpt_runtime_stats_t stats{};
    generate(base, stats);
    for (const auto &prompt :
         {base, base + " She liked to play with her friends.", base.substr(0, 30),
          std::string("Once upon a time there was a little boy."), std::string("A dog saw a cat.")})
    {
        evaluated_tokens = 0;
        const auto cached = generate(prompt, stats);
        const unsigned reused = stats.reused_tokens;
        const unsigned cached_work = evaluated_tokens;
        prefix_cache_enabled = false;
        evaluated_tokens = 0;
        const auto cold = generate(prompt, stats);
        assert(cached == cold);
        assert(stats.reused_tokens == 0);
        assert(cached_work + reused == evaluated_tokens);
        if (prompt == base)
            assert(reused > 0);
        prefix_cache_enabled = true;
        std::printf("equivalent prompt_bytes=%zu reused=%u evaluated=%u cold=%u\n", prompt.size(),
                    reused, cached_work, evaluated_tokens);
    }
    // The last generated token is emitted but not decoded at the output limit.
    const auto first = generate(base, stats, 1);
    const auto repeated = generate(base, stats, 1);
    assert(first == repeated && stats.reused_tokens + 1 == stats.prompt_tokens);
    // A truncated output is a valid prompt cache, without a phantom reply token.
    generate(base, stats, 12, 1);
    const auto after_truncation = generate(base, stats);
    prefix_cache_enabled = false;
    assert(generate(base, stats) == after_truncation);
    prefix_cache_enabled = true;
    // Unsupported rollback, recurrent/hybrid models and an evicted cache start
    // all fall back to the original full-prefill path.
    for (bool *condition :
         {&reject_tail_removal, &recurrent_fixture, &hybrid_fixture, &missing_cache_start})
    {
        *condition = true;
        assert(generate(base, stats) == after_truncation);
        assert(stats.reused_tokens == 0);
        *condition = false;
    }
    // Invalid grammar after prefill must also invalidate the cache.
    gpt_runtime_message_t grammar_message{"user", base.c_str()};
    gpt_runtime_settings_t grammar_settings{};
    grammar_settings.max_output_tokens = 12;
    grammar_settings.grammar = "not a grammar";
    char grammar_output[4096];
    assert(gpt_runtime_generate(&grammar_message, 1, grammar_settings, grammar_output,
                                sizeof(grammar_output), &stats, nullptr) == 1);
    assert(previous_tokens.empty());
    generate(base, stats);
    assert(stats.reused_tokens == 0);
    // Decode errors must invalidate partial memory before the next request.
    fail_next_decode = true;
    gpt_runtime_message_t message{"user", base.c_str()};
    gpt_runtime_settings_t settings{};
    settings.max_output_tokens = 12;
    char output[4096];
    assert(gpt_runtime_generate(&message, 1, settings, output, sizeof(output), &stats, nullptr) ==
           1);
    assert(previous_tokens.empty() && llama_memory_seq_pos_max(llama_get_memory(context), 0) == -1);
    generate(base, stats);
    assert(stats.reused_tokens == 0);
    // Full chat templates can retokenize a generated reply at message joins.
    // Compare the complete second turn against a fresh evaluation as well.
    chat_fixture = true;
    const auto reply = generate(base, stats);
    gpt_runtime_message_t conversation[] = {
        {"user", base.c_str()}, {"assistant", reply.c_str()}, {"user", "What happened next?"}};
    assert(gpt_runtime_generate(conversation, 3, settings, output, sizeof(output), &stats,
                                nullptr) == 0);
    const std::string continued = output;
    assert(stats.reused_tokens > 0);
    prefix_cache_enabled = false;
    assert(gpt_runtime_generate(conversation, 3, settings, output, sizeof(output), &stats,
                                nullptr) == 0);
    assert(continued == output && stats.reused_tokens == 0);
    prefix_cache_enabled = true;
    chat_fixture = false;
    // Model changes discard the cache along with the context.
    assert(gpt_runtime_select_model(1));
    assert(context == nullptr && previous_tokens.empty());
    load_fixture();
    generate(base, stats);
    assert(stats.reused_tokens == 0);
    // A catalog refresh preserves the resident file, but never rebinds its
    // context to a replacement when that selected entry disappears.
    const auto separator = model_path.find_last_of('/');
    const auto fixture_directory = model_path.substr(0, separator);
    prospero::model_root_slot() = fixture_directory.c_str();
    auto *same_context = context;
    gpt_runtime_refresh_models();
    assert(context == same_context);
    generate(base, stats);
    assert(stats.reused_tokens > 0);
    files.push_back({"removed.gguf", model_path});
    selected = files.size() - 1;
    gpt_runtime_refresh_models();
    assert(context == nullptr && previous_tokens.empty() && selected == 0);
    load_fixture();
    // Exceptions from a progress callback cannot leave a reusable context.
    auto throws = +[](const char *) { throw std::runtime_error("test callback failure"); };
    assert(gpt_runtime_generate(&message, 1, settings, output, sizeof(output), &stats, throws) ==
           1);
    assert(context == nullptr && previous_tokens.empty());
    release_model();
    llama_backend_free();
    std::puts("Actual llama.cpp cached/cold output and failure recovery checks passed");
}
