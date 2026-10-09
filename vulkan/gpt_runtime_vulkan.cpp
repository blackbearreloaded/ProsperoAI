// ProsperoAI - llama.cpp inference through the PS5 RADV Vulkan backend.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef PS5_LLAMA_VULKAN
#include "gpt_runtime.hpp"
#ifdef PROSPERO_HYBRID_MEDIA
#include "runtime_vulkan.hpp"
#endif
#include "model_paths.hpp"
#include "llama.h"
#include "ggml-backend.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef PROSPERO_HYBRID_MEDIA
namespace prospero_vulkan {
#endif
extern "C" int sceKernelDebugOutText(int, const char *);
namespace {
struct ModelFile { std::string id, path; };
std::vector<ModelFile> files;
std::mutex runtime_mutex;
bool scanned, initialized, context_full;
bool verbose_logging;
unsigned selected;
llama_model *model;
llama_context *context;

void log_message(ggml_log_level, const char *text, void *) {
    char line[2048];
    std::snprintf(line, sizeof(line), "[ProsperoAI/Vulkan] %s", text);
    sceKernelDebugOutText(0, line);
}
void backend_log_message(ggml_log_level level, const char *text, void *data) {
    // Kernel debug output is synchronous. llama.cpp emits thousands of debug
    // messages while loading vocabulary/tensors and reserving graphs; keep that
    // diagnostic work out of normal startup, while retaining warnings/errors.
    if (verbose_logging || level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR)
        log_message(level, text, data);
}
void scan(const std::string &root, int depth) {
    DIR *dir = opendir(root.c_str());
    if (!dir) {
        char line[512];
        std::snprintf(line, sizeof(line), "models: opendir %s failed errno=%d\n", root.c_str(), errno);
        log_message(GGML_LOG_LEVEL_ERROR, line, nullptr);
        return;
    }
    while (auto *entry = readdir(dir)) {
        if (entry->d_name[0] == '.' || files.size() >= 32) continue;
        std::string path = root + "/" + entry->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            char line[512];
            std::snprintf(line, sizeof(line), "models: stat %s failed errno=%d\n", path.c_str(), errno);
            log_message(GGML_LOG_LEVEL_ERROR, line, nullptr);
            continue;
        }
        if (S_ISDIR(st.st_mode) && depth == 0) scan(path, 1);
        else if (S_ISREG(st.st_mode) && path.size() > 5 && path.substr(path.size() - 5) == ".gguf") {
            std::string id = path.substr(std::strlen(prospero::kModelRoot) + 1);
            if (id.find_first_of("\"\\\r\n") == std::string::npos) files.push_back({id, path});
        }
    }
    closedir(dir);
}
void scan_models() {
    if (scanned) return;
    scanned = true;
    // The per-boot mount helper attaches the shared folder as the title starts.
    // Discovery runs on the app worker; give that narrow mount time to appear.
    struct stat root;
    for (unsigned retry = 0; retry < 60 && stat(prospero::kModelRoot, &root) != 0; ++retry)
        usleep(50000);
    scan(prospero::kModelRoot, 0);
    std::sort(files.begin(), files.end(), [](const ModelFile &a, const ModelFile &b) { return a.id < b.id; });
    char line[128];
    std::snprintf(line, sizeof(line), "models: discovered %zu GGUF files\n", files.size());
    log_message(GGML_LOG_LEVEL_INFO, line, nullptr);
}
void release_model() {
    if (context) llama_free(context);
    if (model) llama_model_free(model);
    context = nullptr;
    model = nullptr;
}
bool prepare() {
    scan_models();
    if (files.empty()) return false;
    if (context) return true;
    const int64_t load_start = ggml_time_us();
    if (!initialized) {
        verbose_logging = access("/app0/vulkan_verbose_logging.txt", F_OK) == 0;
        llama_log_set(backend_log_message, nullptr);
        ggml_log_set(backend_log_message, nullptr);
        setenv("GGML_NO_BACKTRACE", "1", 1);
        llama_backend_init();
        initialized = true;
    }
    ggml_backend_dev_t devices[] = {ggml_backend_dev_by_name("Vulkan0"), nullptr};
    const int64_t device_ready = ggml_time_us();
    if (!devices[0]) return false;
    size_t free_bytes = 0, total_bytes = 0;
    ggml_backend_dev_memory(devices[0], &free_bytes, &total_bytes);
    char memory_line[256];
    std::snprintf(memory_line, sizeof(memory_line),
                  "Vulkan memory: reported free=%zu total=%zu bytes\n", free_bytes, total_bytes);
    log_message(GGML_LOG_LEVEL_INFO, memory_line, nullptr);
    auto mp = llama_model_default_params();
    mp.devices = devices;
    mp.n_gpu_layers = 99;
    mp.load_mode = LLAMA_LOAD_MODE_NONE;
    model = llama_model_load_from_file(files[selected].path.c_str(), mp);
    if (!model) return false;
    const int64_t weights_ready = ggml_time_us();
    auto cp = llama_context_default_params();
    unsigned requested_context = 4096;
    if (FILE *config = std::fopen("/app0/context_size.txt", "r")) {
        unsigned value = 0;
        if (std::fscanf(config, "%u", &value) == 1 && value >= 512 && value <= 16384)
            requested_context = value;
        std::fclose(config);
    }
    cp.n_ctx = std::min<uint32_t>(requested_context, llama_model_n_ctx_train(model));
    cp.n_batch = 256;
    cp.n_ubatch = 256;
    cp.n_threads = 4;
    cp.n_threads_batch = 4;
    cp.no_perf = false;
    cp.offload_kqv = true;
    cp.op_offload = true;
    context = llama_init_from_model(model, cp);
    if (!context) { release_model(); return false; }
    char timing_line[256];
    std::snprintf(timing_line, sizeof(timing_line),
                  "model ready: device=%.2f ms weights=%.2f ms context=%.2f ms total=%.2f ms\n",
                  (device_ready - load_start) / 1000.0,
                  (weights_ready - device_ready) / 1000.0,
                  (ggml_time_us() - weights_ready) / 1000.0,
                  (ggml_time_us() - load_start) / 1000.0);
    log_message(GGML_LOG_LEVEL_INFO, timing_line, nullptr);
    return true;
}
}

bool gpt_runtime_available() { std::lock_guard<std::mutex> lock(runtime_mutex); scan_models(); return !files.empty(); }
unsigned gpt_runtime_model_count() { std::lock_guard<std::mutex> lock(runtime_mutex); scan_models(); return files.size(); }
unsigned gpt_runtime_selected_model() { std::lock_guard<std::mutex> lock(runtime_mutex); return selected; }
const char *gpt_runtime_model_id(unsigned i) { std::lock_guard<std::mutex> lock(runtime_mutex); scan_models(); return i < files.size() ? files[i].id.c_str() : ""; }
const char *gpt_runtime_model_name(unsigned i) { return gpt_runtime_model_id(i); }
const char *gpt_runtime_model_purpose(unsigned) { return "text-to-text"; }
const char *gpt_runtime_name() { return gpt_runtime_model_name(gpt_runtime_selected_model()); }
const char *gpt_runtime_backend() { return "llama.cpp Vulkan / RADV"; }
const char *gpt_runtime_purpose() { return "text-to-text"; }
bool gpt_runtime_context_full() { std::lock_guard<std::mutex> lock(runtime_mutex); return context_full; }
bool gpt_runtime_select_model(unsigned i) {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    scan_models();
    if (i >= files.size()) return false;
    if (i != selected) { release_model(); selected = i; }
    context_full = false;
    return true;
}

void gpt_runtime_refresh_models() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    std::string selected_id = selected < files.size() ? files[selected].id : std::string();
    files.clear(); scanned = false; scan_models();
    auto found = std::find_if(files.begin(), files.end(), [&](const ModelFile &file) { return file.id == selected_id; });
    if (found != files.end()) selected = static_cast<unsigned>(found-files.begin());
    else if (selected >= files.size()) selected = 0;
}

int gpt_runtime_prepare() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    try { return prepare() ? 0 : 1; }
    catch (const std::exception &e) { log_message(GGML_LOG_LEVEL_ERROR, e.what(), nullptr); release_model(); return 1; }
}

int gpt_runtime_generate(const gpt_runtime_message_t *messages, unsigned count,
    const gpt_runtime_settings_t &settings, char *output, std::size_t capacity,
    gpt_runtime_stats_t *stats, gpt_runtime_progress_fn progress) {
    if (!messages || !count || !output || !capacity) return 1;
    std::lock_guard<std::mutex> lock(runtime_mutex);
    output[0] = 0;
    if (stats) *stats = {};
    context_full = false;
    try {
        if (settings.model_id) {
            scan_models();
            auto found = std::find_if(files.begin(), files.end(), [&](const ModelFile &file) {
                return file.id == settings.model_id;
            });
            if (found == files.end()) { std::snprintf(output, capacity, "Requested model is not installed."); return 1; }
            unsigned index = static_cast<unsigned>(found - files.begin());
            if (index != selected) { release_model(); selected = index; }
        }
        if (!prepare()) { std::snprintf(output, capacity, "Unable to load a GGUF model on Vulkan."); return 1; }
        std::string prompt;
        const char *tmpl = llama_model_chat_template(model, nullptr);
        if (tmpl) {
            std::vector<llama_chat_message> chat;
            for (unsigned i = 0; i < count; ++i)
                if (messages[i].role && messages[i].content) chat.push_back({messages[i].role, messages[i].content});
            int n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, nullptr, 0);
            if (n <= 0) { std::snprintf(output, capacity, "Unsupported model chat template."); return 1; }
            std::vector<char> text(n + 1);
            n = llama_chat_apply_template(tmpl, chat.data(), chat.size(), true, text.data(), n);
            if (n <= 0 || n >= (int)text.size()) return 1;
            prompt.assign(text.data(), n);
        } else {
            for (unsigned i = count; i > 0; --i)
                if (messages[i - 1].content && messages[i - 1].role && !std::strcmp(messages[i - 1].role, "user")) {
                    prompt = messages[i - 1].content; break;
                }
        }
        const auto *vocab = llama_model_get_vocab(model);
        int n = -llama_tokenize(vocab, prompt.data(), prompt.size(), nullptr, 0, true, true);
        if (n <= 0) return 1;
        unsigned limit = std::min(settings.max_output_tokens ? settings.max_output_tokens : 128U, 512U);
        if ((unsigned)n + 1 >= llama_n_ctx(context)) {
            context_full = true;
            std::snprintf(output, capacity, "Conversation exceeds this model's context window."); return 1;
        }
        limit = std::min(limit, llama_n_ctx(context) - (unsigned)n);
        std::vector<llama_token> tokens(n);
        if (llama_tokenize(vocab, prompt.data(), prompt.size(), tokens.data(), n, true, true) < 0) return 1;
        llama_memory_clear(llama_get_memory(context), true);
        int64_t start = ggml_time_us();
        for (int pos = 0; pos < n; ) {
            int batch_count = std::min(n - pos, 256);
            auto batch = llama_batch_get_one(tokens.data() + pos, batch_count);
            if (llama_decode(context, batch) != 0) return 1;
            pos += batch_count;
        }
        if (stats) { stats->prompt_tokens = n; stats->prefill_microseconds = ggml_time_us() - start; }
        llama_sampler *sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
        if (settings.grammar) {
            auto *grammar = llama_sampler_init_grammar(vocab, settings.grammar, "root");
            if (!grammar) { llama_sampler_free(sampler); return 1; }
            llama_sampler_chain_add(sampler, grammar);
        }
        if (settings.temperature > 0) {
            llama_sampler_chain_add(sampler, llama_sampler_init_top_k(40));
            llama_sampler_chain_add(sampler, llama_sampler_init_top_p(0.95f, 1));
            llama_sampler_chain_add(sampler, llama_sampler_init_temp(settings.temperature));
            llama_sampler_chain_add(sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        } else llama_sampler_chain_add(sampler, llama_sampler_init_greedy());
        std::size_t used = 0;
        int result = 0;
        for (unsigned i = 0; i < limit; ++i) {
            llama_token token = llama_sampler_sample(sampler, context, -1);
            if (llama_vocab_is_eog(vocab, token)) break;
            char piece[1024];
            int length = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
            if (length < 0) { result = 1; break; }
            if (used + length >= capacity) { if (stats) stats->output_limit_reached = true; break; }
            std::memcpy(output + used, piece, length);
            used += length;
            output[used] = 0;
            if (stats) { ++stats->generated_tokens; stats->output_limit_reached = i + 1 == limit; }
            if (progress) progress(output);
            if (i + 1 < limit) {
                auto batch = llama_batch_get_one(&token, 1);
                if (llama_decode(context, batch) != 0) { result = 1; break; }
            }
        }
        llama_sampler_free(sampler);
        if (stats) stats->elapsed_microseconds = ggml_time_us() - start;
        if (stats) {
            char line[384];
            std::snprintf(line, sizeof(line),
                "gpu_stats rc=%08X prompt=%u generated=%u prefill_us=%llu elapsed_us=%llu\n",
                result, stats->prompt_tokens, stats->generated_tokens,
                static_cast<unsigned long long>(stats->prefill_microseconds),
                static_cast<unsigned long long>(stats->elapsed_microseconds));
            log_message(GGML_LOG_LEVEL_INFO, line, nullptr);
        }
        llama_perf_context_print(context);
        return result;
    } catch (const std::exception &e) {
        log_message(GGML_LOG_LEVEL_ERROR, e.what(), nullptr);
        std::snprintf(output, capacity, "Vulkan inference failed: %s", e.what());
        release_model();
        return 1;
    }
}
#ifdef PROSPERO_HYBRID_MEDIA
void release_model_memory() { std::lock_guard<std::mutex> lock(runtime_mutex); release_model(); }
} // namespace prospero_vulkan
#endif
#endif
