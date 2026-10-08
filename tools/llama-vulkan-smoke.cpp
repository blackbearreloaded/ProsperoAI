// SPDX-License-Identifier: GPL-3.0-or-later
#include "llama.h"
#include "ggml-backend.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

extern "C" int sceKernelDebugOutText(int, const char *);
extern "C" int sceKernelUsleep(unsigned int);
extern "C" int sceSystemServiceLoadExec(const char *, const char *const *);

static void log_line(enum ggml_log_level, const char *text, void *) {
    char line[2048];
    snprintf(line, sizeof(line), "[ProsperoLLAMA] %s", text);
    sceKernelDebugOutText(0, line);
}

static bool run_model(bool gpu, std::vector<llama_token> &output) {
    auto mp = llama_model_default_params();
    mp.n_gpu_layers = gpu ? 99 : 0;
    mp.load_mode = LLAMA_LOAD_MODE_NONE;
    ggml_backend_dev_t devices[] = {ggml_backend_dev_by_name("Vulkan0"), nullptr};
    if (gpu) {
        if (!devices[0]) { log_line(GGML_LOG_LEVEL_ERROR, "No Vulkan GPU\n", nullptr); return false; }
        mp.devices = devices;
    } else {
        mp.devices = &devices[1];
    }
    auto *model = llama_model_load_from_file("/app0/models/stories260K.gguf", mp);
    if (!model) return false;
    auto cp = llama_context_default_params();
    cp.n_ctx = 128;
    cp.n_batch = 64;
    cp.n_ubatch = 64;
    cp.n_threads = 4;
    cp.n_threads_batch = 4;
    cp.no_perf = false;
    cp.offload_kqv = gpu;
    cp.op_offload = gpu;
    auto *ctx = llama_init_from_model(model, cp);
    if (!ctx) { llama_model_free(model); return false; }
    const auto *vocab = llama_model_get_vocab(model);
    const char *prompt = "Once upon a time";
    llama_token tokens[64];
    int count = llama_tokenize(vocab, prompt, strlen(prompt), tokens, 64, true, true);
    if (count <= 0 || count > 64) { llama_free(ctx); llama_model_free(model); return false; }
    auto batch = llama_batch_get_one(tokens, count);
    auto *sampler = llama_sampler_init_greedy();
    bool ok = true;
    int64_t start = ggml_time_us();
    for (int i = 0; i < 32; ++i) {
        int result = llama_decode(ctx, batch);
        if (result != 0) { ok = false; break; }
        tokens[0] = llama_sampler_sample(sampler, ctx, -1);
        output.push_back(tokens[0]);
        char piece[128], line[256];
        int n = llama_token_to_piece(vocab, tokens[0], piece, sizeof(piece) - 1, 0, true);
        if (n >= 0 && n < (int)sizeof(piece)) {
            piece[n] = 0;
            snprintf(line, sizeof(line), "%s token=%d piece=%s\n", gpu ? "Vulkan" : "CPU", tokens[0], piece);
            log_line(GGML_LOG_LEVEL_INFO, line, nullptr);
        }
        if (llama_vocab_is_eog(vocab, tokens[0])) break;
        batch = llama_batch_get_one(tokens, 1);
    }
    double elapsed = (ggml_time_us() - start) / 1000000.0;
    char line[256];
    snprintf(line, sizeof(line), "%s result=%s tokens=%zu elapsed=%.3fs tokens/s=%.2f (includes prefill)\n",
        gpu ? "Vulkan" : "CPU", ok ? "PASS" : "FAIL", output.size(), elapsed, output.size() / elapsed);
    log_line(GGML_LOG_LEVEL_INFO, line, nullptr);
    llama_perf_context_print(ctx);
    llama_sampler_free(sampler);
    llama_free(ctx);
    llama_model_free(model);
    return ok;
}

int main() {
    log_line(GGML_LOG_LEVEL_INFO, "main entered\n", nullptr);
    llama_log_set(log_line, nullptr);
    ggml_log_set(log_line, nullptr);
    setenv("GGML_NO_BACKTRACE", "1", 1);
    try {
        llama_backend_init();
        std::vector<llama_token> gpu, cpu;
        bool gpu_ok = run_model(true, gpu);
        bool cpu_ok = run_model(false, cpu);
        log_line(GGML_LOG_LEVEL_INFO,
            gpu_ok && cpu_ok && gpu == cpu ? "VALIDATION PASS: Vulkan and CPU tokens match\n" : "VALIDATION FAIL\n", nullptr);
        llama_backend_free();
    } catch (const std::exception &e) {
        log_line(GGML_LOG_LEVEL_ERROR, e.what(), nullptr);
    }
    log_line(GGML_LOG_LEVEL_INFO, "test finished; shell exit in 30 seconds\n", nullptr);
    for (int i = 0; i < 300; ++i) sceKernelUsleep(100000);
    sceSystemServiceLoadExec("exit", nullptr);
    for (;;) sceKernelUsleep(100000);
}
