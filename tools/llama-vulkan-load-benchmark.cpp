// SPDX-License-Identifier: GPL-3.0-or-later
#include "llama.h"
#include "ggml-backend.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <atomic>
#include <unistd.h>

extern "C" int sceKernelDebugOutText(int, const char *);
extern "C" int sceKernelUsleep(unsigned int);
static FILE * report;
static bool verbose_logging;
static std::atomic<unsigned long long> log_calls{}, log_bytes{};
static std::atomic<int64_t> log_us{};
static void log_line(ggml_log_level level, const char * text, void *) {
    if (!verbose_logging && level != GGML_LOG_LEVEL_WARN && level != GGML_LOG_LEVEL_ERROR) return;
    const auto begin = ggml_time_us();
    sceKernelDebugOutText(0, text);
    log_us.fetch_add(ggml_time_us() - begin, std::memory_order_relaxed);
    log_calls.fetch_add(1, std::memory_order_relaxed);
    log_bytes.fetch_add(strlen(text), std::memory_order_relaxed);
}
int main() {
    report = fopen("/download0/prospero-load-benchmark.txt", "w");
    if (!report) return 1;
    verbose_logging = access("/app0/vulkan_verbose_logging.txt", F_OK) == 0;
    fprintf(report, "verbose_logging=%d\n", verbose_logging);
    llama_log_set(log_line, nullptr);
    ggml_log_set(log_line, nullptr);
    try {
        const auto start = ggml_time_us();
        llama_backend_init();
        auto * device = ggml_backend_dev_by_name("Vulkan0");
        fprintf(report, "device_init_ms=%.2f\n", (ggml_time_us() - start) / 1000.0);
        fflush(report);
        if (!device) throw std::runtime_error("No Vulkan0");
        for (const size_t mib : {64u, 512u}) {
            const auto begin = ggml_time_us();
            auto * buffer = ggml_backend_buft_alloc_buffer(ggml_backend_dev_buffer_type(device), mib * 1024 * 1024);
            if (!buffer) throw std::runtime_error("Allocation probe failed");
            const auto allocated = ggml_time_us();
            ggml_backend_buffer_clear(buffer, 0);
            const auto cleared = ggml_time_us();
            fprintf(report, "allocation_probe_mib=%zu allocate_ms=%.2f clear_ms=%.2f\n",
                    mib, (allocated - begin) / 1000.0, (cleared - allocated) / 1000.0);
            fflush(report);
            ggml_backend_buffer_free(buffer);
        }
        char path[1024] = {};
        FILE * config = fopen("/app0/load_model_path.txt", "r");
        if (!config || !fgets(path, sizeof(path), config)) throw std::runtime_error("Missing model path");
        fclose(config);
        path[strcspn(path, "\r\n")] = 0;
        const bool parallel_only = access("/app0/load_parallel_only.txt", F_OK) == 0;
        fprintf(report, "parallel_only=%d\n", parallel_only);
        fflush(report);
        ggml_backend_dev_t devices[] = {device, nullptr};
        llama_token reference = LLAMA_TOKEN_NULL;
        for (int round = 0; round < 3; ++round) {
            // Alternate order to expose storage-cache effects rather than silently
            // giving every parallel run the benefit of the previous serial read.
            for (int trial = 0; trial < 2; ++trial) {
                const bool parallel = (round + trial) % 2 != 0;
                if (parallel_only && !parallel) continue;
                if (parallel) unsetenv("PROSPERO_MODEL_IO_SERIAL");
                else setenv("PROSPERO_MODEL_IO_SERIAL", "1", 1);
                auto mp = llama_model_default_params();
                mp.devices = devices;
                mp.n_gpu_layers = 99;
                mp.load_mode = LLAMA_LOAD_MODE_NONE;
                const auto begin = ggml_time_us();
                const auto log_before = log_us.load();
                auto * model = llama_model_load_from_file(path, mp);
                if (!model) throw std::runtime_error("Model load failed");
                const auto loaded = ggml_time_us();
                auto cp = llama_context_default_params();
                cp.n_ctx = 4096;
                cp.n_batch = cp.n_ubatch = 256;
                cp.n_threads = cp.n_threads_batch = 4;
                auto * ctx = llama_init_from_model(model, cp);
                if (!ctx) { llama_model_free(model); throw std::runtime_error("Context failed"); }
                const auto ready = ggml_time_us();
                llama_token tokens[64];
                const auto * vocab = llama_model_get_vocab(model);
                const char * prompt = "Once upon a time";
                const int count = llama_tokenize(vocab, prompt, strlen(prompt), tokens, 64, true, true);
                auto * sampler = llama_sampler_init_greedy();
                const int result = count > 0 ? llama_decode(ctx, llama_batch_get_one(tokens, count)) : -1;
                const auto token = result == 0 ? llama_sampler_sample(sampler, ctx, -1) : LLAMA_TOKEN_NULL;
                const auto evaluated = ggml_time_us();
                if (reference == LLAMA_TOKEN_NULL) reference = token;
                fprintf(report, "round=%d mode=%s weights_ms=%.2f context_ms=%.2f first_eval_ms=%.2f ready_ms=%.2f token=%d match=%d result=%d\n",
                        round + 1, parallel ? "parallel" : "serial",
                        (loaded - begin) / 1000.0, (ready - loaded) / 1000.0,
                        (evaluated - ready) / 1000.0, (ready - begin) / 1000.0,
                        token, token == reference, result);
                fprintf(report, "logger_calls=%llu logger_bytes=%llu logger_total_ms=%.2f logger_round_ms=%.2f\n",
                        log_calls.load(), log_bytes.load(), log_us.load() / 1000.0, (log_us.load() - log_before) / 1000.0);
                fflush(report);
                llama_sampler_free(sampler);
                llama_free(ctx);
                llama_model_free(model);
                if (result != 0 || token != reference) throw std::runtime_error("Token validation failed");
            }
        }
        fprintf(report, "COMPLETE\n");
        llama_backend_free();
    } catch (const std::exception & e) {
        fprintf(report, "ERROR: %s\n", e.what());
    }
    fclose(report);
    for (;;) sceKernelUsleep(100000);
}
