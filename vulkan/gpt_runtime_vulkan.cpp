// ProsperoAI - llama.cpp inference through the PS5 RADV Vulkan backend.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef PS5_LLAMA_VULKAN
#include "gpt_runtime.hpp"
#include "reasoning_prompt.hpp"
#include "minicpm5_chat.hpp"
#include "inference_config.hpp"
#ifdef PROSPERO_VULKAN_RUNTIME
#include "runtime_vulkan.hpp"
#endif
#include "model_paths.hpp"
#include "kv_budget.hpp"
#include "prefix_reuse.hpp"
#include "runtime_settings.hpp"
#include "llama.h"
#include "ggml-backend.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef PROSPERO_VULKAN_RUNTIME
namespace prospero_vulkan {
#endif
extern "C" int sceKernelDebugOutText(int, const char *);
namespace {
struct ModelFile { std::string id, path; };
std::vector<ModelFile> files;
std::mutex runtime_mutex;
bool scanned, initialized, context_full;
bool verbose_logging;
bool prefix_cache_enabled = true;
unsigned selected;
llama_model *model;
llama_context *context;
unsigned requested_context_setting;   // from gpt_runtime_settings_t::context_length
bool requested_kv_is_auto = true;     // from gpt_runtime_settings_t::kv_cache
std::string requested_kv_setting;     // "f16"/"q8_0"/"q4_0" when not Auto
std::vector<llama_token> previous_tokens; // the last turn's token sequence, for prefix reuse

// Derived from llama.cpp's own model-introspection API (llama_model_n_layer/n_head_kv,
// llama_model_is_hybrid - see llama.h), rather than raw GGUF key-value metadata: these are
// the stable, typed accessors llama.cpp's own common/fit.cpp uses for the same purpose.
// A hybrid model's recurrent layers do not all keep a full KV cache (docs/PLAN_CONTEXT_
// MODELS_VULKAN.md's table shows Qwen3.5 9B uses only 8 of 32), but llama.h has no public
// accessor for that subset count; using the full layer count here overstates KV need, which
// only makes Auto pick a smaller context than necessary - safe, not console-verified.
unsigned model_kv_layers(const llama_model *m) { return static_cast<unsigned>(llama_model_n_layer(m)); }
unsigned model_kv_heads(const llama_model *m) { return static_cast<unsigned>(llama_model_n_head_kv(m)); }
unsigned model_head_dim(const llama_model *m) {
    const int heads = llama_model_n_head(m);
    return heads > 0 ? static_cast<unsigned>(llama_model_n_embd(m) / heads) : 0;
}
bool model_is_hybrid(const llama_model *m) { return llama_model_is_hybrid(m) || llama_model_is_recurrent(m); }
enum ggml_type parse_kv_type_setting(const std::string &value) {
    if (value == "q8_0") return GGML_TYPE_Q8_0;
    if (value == "q4_0") return GGML_TYPE_Q4_0;
    return GGML_TYPE_F16;
}

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
            std::string id = path.substr(std::strlen(prospero::model_root()) + 1);
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
    for (unsigned retry = 0; retry < 60 && stat(prospero::model_root(), &root) != 0; ++retry)
        usleep(50000);
    scan(prospero::model_root(), 0);
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
    // The KV cache that previous_tokens describes no longer exists once the context is
    // freed; keeping it would make the next call "reuse" a prefix that was never decoded
    // into the new context.
    previous_tokens.clear();
}
void configure_context(unsigned context_length, const char *kv_cache) {
    const bool kv_is_auto = !kv_cache || !std::strcmp(kv_cache, "auto");
    const std::string kv_setting = kv_cache ? kv_cache : "";
    if (context_length != requested_context_setting || kv_is_auto != requested_kv_is_auto || kv_setting != requested_kv_setting) {
        if (context) llama_free(context);
        context = nullptr;
        previous_tokens.clear();
        requested_context_setting = context_length;
        requested_kv_is_auto = kv_is_auto;
        requested_kv_setting = kv_setting;
    }
}
// A failed decode may leave some ubatches resident. Never advertise those as
// a complete reusable prefix, including errors after prefill or in callbacks.
struct CacheTransaction {
    llama_memory_t memory;
    bool committed = false;
    ~CacheTransaction() {
        if (!committed) {
            llama_memory_clear(memory, true);
            previous_tokens.clear();
        }
    }
};

std::size_t reuse_prompt_prefix(llama_memory_t memory, const std::vector<llama_token> &tokens) {
    std::size_t reused = 0;
    // Recurrent/hybrid state cannot in general rewind to an arbitrary prefix.
    // Keep their original full-prefill behavior until separately validated.
    const bool reusable = prefix_cache_enabled && memory && !previous_tokens.empty() &&
        !llama_model_is_recurrent(model) && !llama_model_is_hybrid(model) &&
        llama_memory_seq_pos_min(memory, 0) == 0 &&
        llama_memory_seq_pos_max(memory, 0) == static_cast<llama_pos>(previous_tokens.size() - 1);
    if (reusable) {
        const auto end = std::min(previous_tokens.size(), tokens.size() - 1);
        while (reused < end && previous_tokens[reused] == tokens[reused]) ++reused;
        // Always evaluate at least the final prompt token to produce fresh
        // logits. A repeated or shortened prompt must discard the old reply.
        if (reused && !llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(reused), -1))
            reused = 0;
    }
    if (!reused) llama_memory_clear(memory, true);
    previous_tokens.resize(reused);
    return reused;
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
    if (!model) model = llama_model_load_from_file(files[selected].path.c_str(), mp);
    if (!model) return false;
    const int64_t weights_ready = ggml_time_us();
    prefix_cache_enabled = access("/app0/disable_prefix_cache.txt", F_OK) != 0;
    auto cp = llama_context_default_params();
    unsigned requested_context = 4096;
    enum ggml_type kv_type = GGML_TYPE_F16;
    prospero_kv::ModelKvShape shape{model_kv_layers(model), model_kv_heads(model),
                                   model_head_dim(model), static_cast<unsigned>(llama_model_n_ctx_train(model))};
    // Query the remaining heap after weights load: do not subtract weights a second time.
    ggml_backend_dev_memory(devices[0], &free_bytes, &total_bytes);
    free_bytes = prospero_kv::validated_free_bytes(free_bytes, total_bytes);
    const auto fixed_type = requested_kv_setting == "q8_0" ? prospero_kv::KvType::Q8_0
                          : requested_kv_setting == "q4_0" ? prospero_kv::KvType::Q4_0 : prospero_kv::KvType::F16;
    const auto choice = prospero_kv::choose_auto(shape, 0, 1536ULL * 1024 * 1024, free_bytes,
                                               requested_kv_is_auto ? nullptr : &fixed_type);
    requested_context = requested_context_setting ? requested_context_setting : choice.context_length;
    if (FILE *config = std::fopen("/app0/context_size.txt", "r")) {
        unsigned value = 0;
        if (std::fscanf(config, "%u", &value) == 1 && value >= 512 && value <= 16384)
            requested_context = value;
        std::fclose(config);
    }
    cp.n_ctx = std::min<uint32_t>(requested_context, llama_model_n_ctx_train(model));
    kv_type = requested_kv_is_auto ? (cp.n_ctx <= 8192 ? GGML_TYPE_F16 : GGML_TYPE_Q8_0)
                                   : parse_kv_type_setting(requested_kv_setting);
    cp.type_k = kv_type;
    cp.type_v = kv_type;
    cp.n_batch = prospero::read_inference_setting("/app0", "batch_size.txt", 256, 32, 2048);
    cp.n_ubatch = std::min(cp.n_batch, prospero::read_inference_setting(
        "/app0", "ubatch_size.txt", 256, 32, 2048));
    cp.n_threads = prospero::read_inference_setting("/app0", "threads.txt", 4, 1, 16);
    cp.n_threads_batch = prospero::read_inference_setting("/app0", "threads_batch.txt", 4, 1, 16);
    // 0=auto, 1=disabled, 2=enabled. Auto retains backend support checks.
    const unsigned flash = prospero::read_inference_setting("/app0", "flash_attention.txt", 0, 0, 2);
    cp.flash_attn_type = flash == 1 ? LLAMA_FLASH_ATTN_TYPE_DISABLED :
                        flash == 2 ? LLAMA_FLASH_ATTN_TYPE_ENABLED : LLAMA_FLASH_ATTN_TYPE_AUTO;
    if (kv_type != GGML_TYPE_F16) cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_ENABLED;
    cp.no_perf = false;
    cp.offload_kqv = true;
    cp.op_offload = true;
    context = llama_init_from_model(model, cp);
    if (!context) { previous_tokens.clear(); return false; }
    char config_line[256];
    std::snprintf(config_line, sizeof(config_line),
                  "inference config: requested=%u effective_ctx=%u kv=%s batch=%u ubatch=%u threads=%d threads_batch=%d flash=%s\n",
                  requested_context, llama_n_ctx(context), ggml_type_name(kv_type),
                  llama_n_batch(context), llama_n_ubatch(context), cp.n_threads, cp.n_threads_batch,
                  llama_flash_attn_type_name(cp.flash_attn_type));
    log_message(GGML_LOG_LEVEL_INFO, config_line, nullptr);
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
    else { release_model(); selected = 0; }
}

int gpt_runtime_prepare() {
    std::lock_guard<std::mutex> lock(runtime_mutex);
    try {
        const auto settings = prospero_settings::get();
        configure_context(settings.context, "auto");
        return prepare() ? 0 : 1;
    }
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
        if (settings.context_tokens && (settings.context_tokens < 512 || settings.context_tokens > 16384)) {
            std::snprintf(output, capacity, "Context window must be between 512 and 16384 tokens."); return 1;
        }
        configure_context(settings.context_tokens ? settings.context_tokens : settings.context_length, settings.kv_cache);
        if (!prepare()) { std::snprintf(output, capacity, "Unable to load a GGUF model on Vulkan."); return 1; }
        std::string prompt;
        const char *tmpl = llama_model_chat_template(model, nullptr);
        char tokenizer_pre[32] = {};
        llama_model_meta_val_str(model, "tokenizer.ggml.pre", tokenizer_pre, sizeof(tokenizer_pre));
        const bool minicpm5 = std::strcmp(tokenizer_pre, "minicpm5") == 0;
        if (minicpm5) {
            if (!prospero::minicpm5_chat(messages, count, prompt)) {
                std::snprintf(output, capacity, "MiniCPM5 supports system, user and assistant text messages.");
                return 1;
            }
        } else if (tmpl) {
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
        prospero::reasoning_prompt(prompt, tmpl, minicpm5, settings.thinking);
        const auto *vocab = llama_model_get_vocab(model);
        int n = -llama_tokenize(vocab, prompt.data(), prompt.size(), nullptr, 0, !minicpm5, true);
        if (n <= 0) return 1;
        unsigned limit = std::min(settings.max_output_tokens ? settings.max_output_tokens : 128U, 4096U);
        if ((unsigned)n + 1 >= llama_n_ctx(context)) {
            context_full = true;
            std::snprintf(output, capacity, "Conversation exceeds this model's context window."); return 1;
        }
        limit = std::min(limit, llama_n_ctx(context) - (unsigned)n);
        std::vector<llama_token> tokens(n);
        if (llama_tokenize(vocab, prompt.data(), prompt.size(), tokens.data(), n, !minicpm5, true) < 0) return 1;
        const auto memory = llama_get_memory(context);
        CacheTransaction cache_transaction{memory};
        int64_t start = ggml_time_us();
        const auto reused = reuse_prompt_prefix(memory, tokens);
        previous_tokens.reserve(llama_n_ctx(context));
        if (stats) stats->reused_tokens = static_cast<unsigned>(reused);
        for (int pos = static_cast<int>(reused); pos < n; ) {
            int batch_count = std::min(n - pos, static_cast<int>(llama_n_batch(context)));
            auto batch = llama_batch_get_one(tokens.data() + pos, batch_count);
            if (llama_decode(context, batch) != 0) { previous_tokens.clear(); return 1; }
            previous_tokens.insert(previous_tokens.end(), tokens.begin() + pos, tokens.begin() + pos + batch_count);
            pos += batch_count;
        }
        if (stats) { stats->prompt_tokens = n; stats->prefill_microseconds = ggml_time_us() - start; }
        std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler(
            llama_sampler_chain_init(llama_sampler_chain_default_params()), llama_sampler_free);
        if (settings.grammar) {
            auto *grammar = llama_sampler_init_grammar(vocab, settings.grammar, "root");
            if (!grammar) return 1;
            llama_sampler_chain_add(sampler.get(), grammar);
        }
        if (settings.temperature > 0) {
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(40));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(0.95f, 1));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(settings.temperature));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        } else llama_sampler_chain_add(sampler.get(), llama_sampler_init_greedy());
        std::size_t used = 0;
        // Qwen's opening marker is already in the prompt. Include it in the
        // displayed response so clients can separate reasoning from the answer.
        // It is not another generated token or another KV entry.
        if (settings.thinking && prompt.ends_with("<think>\n")) {
            constexpr char marker[] = "<think>\n";
            if (capacity <= sizeof(marker) - 1) return 1;
            std::memcpy(output, marker, sizeof(marker));
            used = sizeof(marker) - 1;
            if (progress) progress(output);
        }
        int result = 0;
        for (unsigned i = 0; i < limit; ++i) {
            llama_token token = llama_sampler_sample(sampler.get(), context, -1);
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
                if (llama_decode(context, batch) != 0) { previous_tokens.clear(); result = 1; break; }
                previous_tokens.push_back(token);
            }
        }
        sampler.reset();
        cache_transaction.committed = result == 0;
        if (stats) stats->elapsed_microseconds = ggml_time_us() - start;
        if (stats) {
            char line[384];
            std::snprintf(line, sizeof(line),
                "gpu_stats rc=%08X prompt=%u generated=%u reused=%u prefill_us=%llu elapsed_us=%llu\n",
                result, stats->prompt_tokens, stats->generated_tokens, stats->reused_tokens,
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
#ifdef PROSPERO_VULKAN_RUNTIME
void release_model_memory() { std::lock_guard<std::mutex> lock(runtime_mutex); release_model(); }
} // namespace prospero_vulkan
#endif
#endif
