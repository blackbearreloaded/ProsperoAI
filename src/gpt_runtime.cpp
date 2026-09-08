// ProsperoAI adapter for the native PS5 AGC model backends.

#include "gpt_runtime.hpp"
#include "model_metadata.hpp"

#if defined(PS5_MEDIA_AUDIO) && !defined(PS5_DUAL_BACKEND)
#error "PS5 media routing requires the universal model backend"
#endif

extern "C"
{
#include "chat_prompt.h"

#ifdef PS5_DUAL_BACKEND
#define DECLARE_MODEL_BACKEND(prefix)                                                              \
    int prefix##_run_model_chat(const ps5_chat_message_t *, std::uint32_t, std::uint32_t,          \
                                void (*)(const char *));                                           \
    int prefix##_ps5_compute_select_model_files(const char *, const char *);                       \
    void prefix##_ps5_compute_shutdown(void);                                                      \
    extern std::uint32_t prefix##_ps5_compute_generated_count;                                     \
    extern std::uint32_t prefix##_ps5_compute_generated_tokens[];                                  \
    extern std::uint32_t prefix##_ps5_compute_context_full;                                        \
    extern std::uint32_t prefix##_ps5_compute_prompt_count;                                        \
    extern std::uint32_t prefix##_ps5_compute_kv_reused;                                           \
    extern std::uint32_t prefix##_ps5_compute_model_reused;                                        \
    extern std::uint32_t prefix##_ps5_compute_stage;                                               \
    extern std::uint32_t prefix##_ps5_compute_model_path;                                          \
    extern std::uint32_t prefix##_ps5_compute_tokenizer_path;                                      \
    extern std::uint64_t prefix##_ps5_compute_elapsed_us;                                          \
    extern std::uint64_t prefix##_ps5_compute_load_us;                                             \
    extern std::uint64_t prefix##_ps5_compute_prefill_us;                                          \
    extern char prefix##_ps5_compute_response[4096]

    DECLARE_MODEL_BACKEND(mistral);
    DECLARE_MODEL_BACKEND(qwen35);
#undef DECLARE_MODEL_BACKEND
#else
    int run_model_chat(const ps5_chat_message_t *, std::uint32_t, std::uint32_t,
                       void (*)(const char *));
    int ps5_compute_select_model_files(const char *, const char *);
    void ps5_compute_shutdown(void);
    extern std::uint32_t ps5_compute_generated_count;
    extern std::uint32_t ps5_compute_generated_tokens[];
#if defined(PS5_QWEN_DIAGNOSTIC_PHASES) || defined(PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES)
    extern std::uint32_t ps5_compute_debug_state_bits[16];
#endif
    extern std::uint32_t ps5_compute_context_full;
    extern std::uint32_t ps5_compute_prompt_count;
    extern std::uint32_t ps5_compute_kv_reused;
    extern std::uint32_t ps5_compute_model_reused;
    extern std::uint32_t ps5_compute_stage;
    extern std::uint32_t ps5_compute_model_path;
    extern std::uint32_t ps5_compute_tokenizer_path;
    extern std::uint64_t ps5_compute_elapsed_us;
    extern std::uint64_t ps5_compute_load_us;
    extern std::uint64_t ps5_compute_prefill_us;
#ifdef PS5_GPU_TIMING
    extern std::uint64_t ps5_compute_gpu_ticks[11];
    extern std::uint32_t ps5_compute_gpu_samples;
#endif
#endif
    extern std::uint32_t ps5_tokenizer_load_stage;
    extern std::uint64_t ps5_tokenizer_load_detail;
#ifdef PS5_MEDIA_AUDIO
    int ps5_stable_audio_generate(const char *, char *, std::size_t, std::uint64_t *,
                                  void (*)(const char *));
    int ps5_pocket_tts_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                                void (*)(const char *));
    int ps5_kokoro_tts_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                                void (*)(const char *));
    int ps5_agc_backend_release_scratch(void);
    void ps5_media_stop(void);
#endif
#ifdef PS5_MEDIA_IMAGE
    int ps5_sd_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                        void (*)(const char *));
    void ps5_sd_shutdown(void);
    bool ps5SdReleaseDirectArenaIfEmpty() noexcept;
    void ps5SetDirectFallback(int) noexcept;
#endif
#ifndef PS5_DUAL_BACKEND
    extern char ps5_compute_response[4096];
#endif
    int sceKernelClose(int);
    int sceKernelGetdents(int, char *, int);
    int sceKernelOpen(const char *, int, int);
    int sceKernelDebugOutText(int channel, const char *text);
}

#include <cstdio>
#include <cstring>
#include <sys/dirent.h>

namespace
{
#ifdef PS5_GPU_TIMING
void log_gpu_timing()
{
    char line[512]{};
    std::snprintf(line, sizeof(line),
                  "[prosperogpt] gpu_ticks samples=%u prepare=%llu qkv=%llu rope=%llu "
                  "context=%llu attn_quant=%llu output=%llu ff_prepare=%llu "
                  "gate_up=%llu quant=%llu down=%llu logits=%llu\n",
                  ps5_compute_gpu_samples,
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[0]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[1]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[2]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[3]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[4]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[5]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[6]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[7]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[8]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[9]),
                  static_cast<unsigned long long>(ps5_compute_gpu_ticks[10]));
    sceKernelDebugOutText(0, line);
}
#endif

constexpr unsigned kMaximumModels = 8;

enum class RuntimeArchitecture : unsigned
{
    Unknown,
    Mistral7B,
    Qwen35,
#ifdef PS5_MEDIA_IMAGE
    StableDiffusion,
#endif
#ifdef PS5_MEDIA_AUDIO
    StableAudio,
    PocketTTS,
    KokoroTTS,
#endif
};

#ifdef PS5_MEDIA_IMAGE
bool uses_direct_fallback(RuntimeArchitecture architecture)
{
    if (architecture == RuntimeArchitecture::StableDiffusion)
        return true;
#ifdef PS5_MEDIA_AUDIO
    return architecture == RuntimeArchitecture::StableAudio;
#else
    return false;
#endif
}
#endif

#ifdef PS5_DUAL_BACKEND
struct RuntimeBackend
{
    int (*run_chat)(const ps5_chat_message_t *, std::uint32_t, std::uint32_t,
                    void (*)(const char *));
    int (*select_files)(const char *, const char *);
    void (*shutdown)();
    std::uint32_t *generated_count;
    std::uint32_t *generated_tokens;
    std::uint32_t *context_full;
    std::uint32_t *prompt_count;
    std::uint32_t *kv_reused;
    std::uint32_t *model_reused;
    std::uint32_t *stage;
    std::uint32_t *model_path;
    std::uint32_t *tokenizer_path;
    std::uint64_t *elapsed_us;
    std::uint64_t *load_us;
    std::uint64_t *prefill_us;
    char *response;
};

// clang-format off
#define MODEL_BACKEND(prefix)                                                                      \
    {                                                                                              \
        prefix##_run_model_chat,                                                                   \
        prefix##_ps5_compute_select_model_files,                                                   \
        prefix##_ps5_compute_shutdown,                                                             \
        &prefix##_ps5_compute_generated_count,                                                     \
        prefix##_ps5_compute_generated_tokens,                                                     \
        &prefix##_ps5_compute_context_full,                                                        \
        &prefix##_ps5_compute_prompt_count,                                                        \
        &prefix##_ps5_compute_kv_reused,                                                           \
        &prefix##_ps5_compute_model_reused,                                                        \
        &prefix##_ps5_compute_stage,                                                               \
        &prefix##_ps5_compute_model_path,                                                          \
        &prefix##_ps5_compute_tokenizer_path,                                                      \
        &prefix##_ps5_compute_elapsed_us,                                                          \
        &prefix##_ps5_compute_load_us,                                                             \
        &prefix##_ps5_compute_prefill_us,                                                          \
        prefix##_ps5_compute_response,                                                             \
    }
// clang-format on

RuntimeBackend mistral_backend = MODEL_BACKEND(mistral);
RuntimeBackend qwen35_backend = MODEL_BACKEND(qwen35);
#undef MODEL_BACKEND
RuntimeBackend *runtime_backend;

RuntimeBackend *backend_for(RuntimeArchitecture architecture)
{
    if (architecture == RuntimeArchitecture::Mistral7B)
        return &mistral_backend;
    if (architecture == RuntimeArchitecture::Qwen35)
        return &qwen35_backend;
    return nullptr;
}

#define run_model_chat runtime_backend->run_chat
#define ps5_compute_generated_count (*runtime_backend->generated_count)
#define ps5_compute_generated_tokens runtime_backend->generated_tokens
#define ps5_compute_context_full (*runtime_backend->context_full)
#define ps5_compute_prompt_count (*runtime_backend->prompt_count)
#define ps5_compute_kv_reused (*runtime_backend->kv_reused)
#define ps5_compute_model_reused (*runtime_backend->model_reused)
#define ps5_compute_stage (*runtime_backend->stage)
#define ps5_compute_model_path (*runtime_backend->model_path)
#define ps5_compute_tokenizer_path (*runtime_backend->tokenizer_path)
#define ps5_compute_elapsed_us (*runtime_backend->elapsed_us)
#define ps5_compute_load_us (*runtime_backend->load_us)
#define ps5_compute_prefill_us (*runtime_backend->prefill_us)
#define ps5_compute_response runtime_backend->response
#endif

struct RuntimeModel
{
    char id[48];
    char name[64];
    char purpose[24];
    char root[192];
    char model_file[192];
    char tokenizer_file[192];
    RuntimeArchitecture architecture;
};

struct ModelIdentityHeader
{
    char magic[4];
    std::uint32_t version;
    std::uint32_t header_bytes;
    std::uint32_t entry_bytes;
    std::uint32_t tensor_count;
    std::uint32_t block_count;
    std::uint32_t context_length;
    std::uint32_t embedding_length;
    std::uint32_t feed_forward_length;
    std::uint32_t head_count;
    std::uint32_t head_count_kv;
    std::uint32_t vocab_size;
};
static_assert(sizeof(ModelIdentityHeader) == 48, "P5LM identity header layout changed");

RuntimeModel models[kMaximumModels]{};
alignas(16) char directory_entries[0x40000];
unsigned model_count;
unsigned selected_model;
bool models_loaded;

#ifdef PS5_QWEN_SMOKE_TEST
void smoke_progress(const char *)
{
}
#endif

bool asset_exists(const char *path)
{
    std::FILE *file = std::fopen(path, "rb");
    if (!file)
        return false;
    std::fclose(file);
    return true;
}

bool valid_model_id(const char *id)
{
    if (!id || !*id || std::strlen(id) >= sizeof(models[0].id))
        return false;
    for (const char *at = id; *at; ++at)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '-' || *at == '_' || *at == '.'))
            return false;
    return std::strcmp(id, ".") != 0 && std::strcmp(id, "..") != 0;
}

RuntimeArchitecture model_architecture(const char *path)
{
    ModelIdentityHeader header{};
    std::FILE *file = std::fopen(path, "rb");
    const bool read = file && std::fread(&header, 1, sizeof(header), file) == sizeof(header);
    if (file)
        std::fclose(file);
    if (!read || std::memcmp(header.magic, "P5LM", 4) != 0 || header.version != 1 ||
        header.header_bytes != 256 || header.entry_bytes != 128 || header.block_count != 32 ||
        header.embedding_length != 4096)
        return RuntimeArchitecture::Unknown;
    if (header.feed_forward_length == 14336 && header.head_count == 32 &&
        header.head_count_kv == 8 && header.vocab_size == 32768)
        return RuntimeArchitecture::Mistral7B;
    if (header.feed_forward_length == 12288 && header.head_count == 16 &&
        header.head_count_kv == 4 && header.vocab_size == 248320)
        return RuntimeArchitecture::Qwen35;
    return RuntimeArchitecture::Unknown;
}

bool architecture_supported(RuntimeArchitecture architecture)
{
#ifdef PS5_MEDIA_IMAGE
    if (architecture == RuntimeArchitecture::StableDiffusion)
        return true;
#endif
#ifdef PS5_MEDIA_AUDIO
    if (architecture == RuntimeArchitecture::StableAudio ||
        architecture == RuntimeArchitecture::PocketTTS ||
        architecture == RuntimeArchitecture::KokoroTTS)
        return true;
#endif
#ifdef PS5_DUAL_BACKEND
    return backend_for(architecture) != nullptr;
#elif defined(PS5_MODEL_QWEN35)
    return architecture == RuntimeArchitecture::Qwen35;
#else
    return architecture == RuntimeArchitecture::Mistral7B;
#endif
}

void add_model(const char *id, const char *name, const char *purpose, const char *root,
               const char *model_file, const char *tokenizer_file, RuntimeArchitecture architecture)
{
    if (model_count >= kMaximumModels || !valid_model_id(id) || !name || !*name || !purpose ||
        !*purpose || !root || !*root || !architecture_supported(architecture))
        return;
#ifdef PS5_MEDIA_AUDIO
    if (architecture == RuntimeArchitecture::StableAudio)
    {
        char weight[256];
        char encoder[256];
        std::snprintf(weight, sizeof(weight), "%s/weights/model.fp16.bin", root);
        std::snprintf(encoder, sizeof(encoder), "%s/weights/t5_encoder.fp16.bin", root);
        if (!asset_exists(weight) || !asset_exists(encoder))
            return;
    }
    else if (architecture == RuntimeArchitecture::PocketTTS)
    {
        char backbone[256];
        char projector[256];
        char speaker[256];
        std::snprintf(backbone, sizeof(backbone), "%s/pocket-tts.gguf", root);
        std::snprintf(projector, sizeof(projector), "%s/mmproj-pocket-tts.gguf", root);
        std::snprintf(speaker, sizeof(speaker), "%s/speaker.wav", root);
        if (!asset_exists(backbone) || !asset_exists(projector) || !asset_exists(speaker))
            return;
    }
    else if (architecture == RuntimeArchitecture::KokoroTTS)
    {
        char weights[256];
        char metadata[256];
        char voice[256];
        char vocab[256];
        std::snprintf(weights, sizeof(weights), "%s/weights/model.fp16.bin", root);
        std::snprintf(metadata, sizeof(metadata), "%s/weights/model.fp16.meta.json", root);
        std::snprintf(voice, sizeof(voice), "%s/assets/voice_pack_af_heart.bin", root);
        std::snprintf(vocab, sizeof(vocab), "%s/assets/phoneme_vocab.tsv", root);
        if (!asset_exists(weights) || !asset_exists(metadata) || !asset_exists(voice) ||
            !asset_exists(vocab))
            return;
    }
    else
#endif
#ifdef PS5_MEDIA_IMAGE
        if (architecture == RuntimeArchitecture::StableDiffusion)
    {
        char text_encoder[256];
        char diffusion[256];
        char vae[256];
        std::snprintf(text_encoder, sizeof(text_encoder), "%s/text_encoder/model.safetensors",
                      root);
        std::snprintf(diffusion, sizeof(diffusion), "%s/unet/diffusion_pytorch_model.safetensors",
                      root);
        std::snprintf(vae, sizeof(vae), "%s/vae/diffusion_pytorch_model.safetensors", root);
        if (!asset_exists(text_encoder) || !asset_exists(diffusion) || !asset_exists(vae))
            return;
    }
    else
#endif
        if (!asset_exists(model_file) || !asset_exists(tokenizer_file))
        return;
    for (unsigned i = 0; i < model_count; ++i)
        if (std::strcmp(models[i].id, id) == 0)
            return;
    RuntimeModel &model = models[model_count++];
    std::snprintf(model.id, sizeof(model.id), "%s", id);
    std::snprintf(model.name, sizeof(model.name), "%s", name);
    std::snprintf(model.purpose, sizeof(model.purpose), "%s", purpose);
    std::snprintf(model.root, sizeof(model.root), "%s", root);
    std::snprintf(model.model_file, sizeof(model.model_file), "%s", model_file);
    std::snprintf(model.tokenizer_file, sizeof(model.tokenizer_file), "%s", tokenizer_file);
    model.architecture = architecture;
}

void read_model_metadata(const char *path, const char *fallback, char *name,
                         std::size_t name_capacity, char *purpose, std::size_t purpose_capacity,
                         char *runtime, std::size_t runtime_capacity)
{
    std::snprintf(name, name_capacity, "%s", fallback);
    std::snprintf(purpose, purpose_capacity, "text-to-text");
    runtime[0] = '\0';
    if (std::FILE *file = std::fopen(path, "rb"))
    {
        char json[512]{};
        char parsed_name[64]{};
        char parsed_purpose[24]{};
        char parsed_runtime[48]{};
        const std::size_t bytes = std::fread(json, 1, sizeof(json) - 1, file);
        if (bytes && ps5_model_name_from_json(json, parsed_name, sizeof(parsed_name)))
            std::snprintf(name, name_capacity, "%s", parsed_name);
        if (bytes && ps5_model_purpose_from_json(json, parsed_purpose, sizeof(parsed_purpose)))
            std::snprintf(purpose, purpose_capacity, "%s", parsed_purpose);
        if (bytes && ps5_model_runtime_from_json(json, parsed_runtime, sizeof(parsed_runtime)))
            std::snprintf(runtime, runtime_capacity, "%s", parsed_runtime);
        std::fclose(file);
    }
}

RuntimeArchitecture directory_architecture(const char *purpose, const char *runtime,
                                           const char *model_file)
{
#ifdef PS5_MEDIA_IMAGE
    if (std::strcmp(purpose, "text-to-image") == 0 &&
        std::strcmp(runtime, "stable-diffusion-cpp-sd2") == 0)
        return RuntimeArchitecture::StableDiffusion;
#endif
#ifdef PS5_MEDIA_AUDIO
    if (std::strcmp(purpose, "text-to-audio") == 0 &&
        std::strcmp(runtime, "stable-audio-open-small") == 0)
        return RuntimeArchitecture::StableAudio;
    if (std::strcmp(purpose, "text-to-speech") == 0 &&
        std::strcmp(runtime, "llama.cpp-pocket-tts") == 0)
        return RuntimeArchitecture::PocketTTS;
    if (std::strcmp(purpose, "text-to-speech") == 0 &&
        std::strcmp(runtime, "kokoro-82m-ps5agc") == 0)
        return RuntimeArchitecture::KokoroTTS;
#endif
    return std::strcmp(purpose, "text-to-text") == 0 ? model_architecture(model_file)
                                                     : RuntimeArchitecture::Unknown;
}

void sort_models()
{
    for (unsigned i = 1; i < model_count; ++i)
        for (unsigned j = i; j > 0 && std::strcmp(models[j - 1].id, models[j].id) > 0; --j)
        {
            const RuntimeModel swap = models[j - 1];
            models[j - 1] = models[j];
            models[j] = swap;
        }
}

void load_models()
{
    if (models_loaded)
        return;
    models_loaded = true;

    const int directory = sceKernelOpen("/app0/models", 0, 0);
    int directory_bytes = -1;
    if (directory >= 0)
    {
        std::memset(directory_entries, 0, sizeof(directory_entries));
        directory_bytes =
            sceKernelGetdents(directory, directory_entries, sizeof(directory_entries));
        if (directory_bytes > 0)
        {
            int offset = 0;
            while (model_count < kMaximumModels &&
                   offset + static_cast<int>(offsetof(dirent, d_name)) + 1 <= directory_bytes)
            {
                const auto *entry = reinterpret_cast<const dirent *>(directory_entries + offset);
                const int name_offset = static_cast<int>(offsetof(dirent, d_name));
                if (entry->d_reclen <= name_offset || offset + entry->d_reclen > directory_bytes)
                    break;
                if (valid_model_id(entry->d_name))
                {
                    char model_file[192];
                    char tokenizer_file[192];
                    char metadata_file[192];
                    char root[192];
                    char name[64];
                    char purpose[24];
                    char runtime[48];
                    std::snprintf(root, sizeof(root), "/app0/models/%s", entry->d_name);
                    std::snprintf(model_file, sizeof(model_file), "/app0/models/%s/model.ps5lm",
                                  entry->d_name);
                    std::snprintf(tokenizer_file, sizeof(tokenizer_file),
                                  "/app0/models/%s/tokenizer.ps5tok", entry->d_name);
                    std::snprintf(metadata_file, sizeof(metadata_file),
                                  "/app0/models/%s/model.json", entry->d_name);
                    read_model_metadata(metadata_file, entry->d_name, name, sizeof(name), purpose,
                                        sizeof(purpose), runtime, sizeof(runtime));
                    add_model(entry->d_name, name, purpose, root, model_file, tokenizer_file,
                              directory_architecture(purpose, runtime, model_file));
                }
                offset += entry->d_reclen;
            }
        }
        sceKernelClose(directory);
    }
    sort_models();
    if (model_count)
    {
#ifdef PS5_MEDIA_IMAGE
        ps5SetDirectFallback(uses_direct_fallback(models[0].architecture));
#endif
#ifdef PS5_DUAL_BACKEND
        runtime_backend = backend_for(models[0].architecture);
        if (runtime_backend)
            runtime_backend->select_files(models[0].model_file, models[0].tokenizer_file);
#else
        ps5_compute_select_model_files(models[0].model_file, models[0].tokenizer_file);
#endif
    }
    char line[160];
    std::snprintf(line, sizeof(line),
                  "[prosperogpt] models_found=%u directory_fd=%d "
                  "directory_bytes=%d path=/app0/models\n",
                  model_count, directory, directory_bytes);
    sceKernelDebugOutText(0, line);
}
} // namespace

bool gpt_runtime_available()
{
    load_models();
    return model_count != 0;
}

const char *gpt_runtime_name()
{
    load_models();
    return model_count ? models[selected_model].name : "No models found";
}

unsigned gpt_runtime_model_count()
{
    load_models();
    return model_count;
}

unsigned gpt_runtime_selected_model()
{
    load_models();
    return selected_model;
}

const char *gpt_runtime_model_id(unsigned index)
{
    load_models();
    return index < model_count ? models[index].id : "";
}

const char *gpt_runtime_model_name(unsigned index)
{
    load_models();
    return index < model_count ? models[index].name : "No models found";
}

const char *gpt_runtime_model_purpose(unsigned index)
{
    load_models();
    return index < model_count ? models[index].purpose : "unknown";
}

const char *gpt_runtime_purpose()
{
    load_models();
    return model_count ? models[selected_model].purpose : "unknown";
}

bool gpt_runtime_select_model(unsigned index)
{
    load_models();
    if (index >= model_count)
        return false;
    if (index == selected_model)
        return true;
#ifdef PS5_MEDIA_AUDIO
    if (models[selected_model].architecture == RuntimeArchitecture::StableAudio ||
        models[selected_model].architecture == RuntimeArchitecture::PocketTTS ||
        models[selected_model].architecture == RuntimeArchitecture::KokoroTTS)
        ps5_media_stop();
#endif
#ifdef PS5_DUAL_BACKEND
#ifdef PS5_MEDIA_IMAGE
    if (models[selected_model].architecture == RuntimeArchitecture::StableDiffusion)
        ps5_sd_shutdown();
#endif
    RuntimeBackend *next = backend_for(models[index].architecture);
#ifdef PS5_MEDIA_IMAGE
    if (models[index].architecture == RuntimeArchitecture::StableDiffusion)
    {
        if (runtime_backend)
            runtime_backend->shutdown();
        runtime_backend = nullptr;
    }
    else
#endif
#ifdef PS5_MEDIA_AUDIO
        if (models[index].architecture == RuntimeArchitecture::StableAudio ||
            models[index].architecture == RuntimeArchitecture::PocketTTS ||
            models[index].architecture == RuntimeArchitecture::KokoroTTS)
    {
        if (runtime_backend)
            runtime_backend->shutdown();
        runtime_backend = nullptr;
    }
    else
#endif
    {
        if (!next ||
            next->select_files(models[index].model_file, models[index].tokenizer_file) != 0)
            return false;
        if (runtime_backend && runtime_backend != next)
            runtime_backend->shutdown();
        runtime_backend = next;
    }
#else
    if (ps5_compute_select_model_files(models[index].model_file, models[index].tokenizer_file) != 0)
        return false;
#endif
#ifdef PS5_MEDIA_IMAGE
    ps5SetDirectFallback(uses_direct_fallback(models[index].architecture));
#endif
    selected_model = index;
    char line[192];
    std::snprintf(line, sizeof(line), "[prosperogpt] selected_model=%s purpose=%s\n",
                  models[index].name, models[index].purpose);
    sceKernelDebugOutText(0, line);
    return true;
}

const char *gpt_runtime_backend()
{
#ifdef PS5_MEDIA_IMAGE
    load_models();
    if (model_count && models[selected_model].architecture == RuntimeArchitecture::StableDiffusion)
        return "Native AGC GPU · Image";
#endif
#ifdef PS5_MEDIA_AUDIO
    load_models();
    if (model_count && models[selected_model].architecture == RuntimeArchitecture::StableAudio)
        return "Native AGC GPU · Audio";
    if (model_count && models[selected_model].architecture == RuntimeArchitecture::PocketTTS)
        return "Native AGC GPU · Speech";
    if (model_count && models[selected_model].architecture == RuntimeArchitecture::KokoroTTS)
        return "Native AGC GPU · Speech";
#endif
    return "Native AGC GPU";
}

bool gpt_runtime_context_full()
{
#ifdef PS5_MEDIA_AUDIO
    if (!runtime_backend)
        return false;
#endif
    return ps5_compute_context_full != 0;
}

int gpt_runtime_prepare()
{
    if (!gpt_runtime_available())
        return 1;
#ifdef PS5_MEDIA_IMAGE
    if (models[selected_model].architecture == RuntimeArchitecture::StableDiffusion)
        return 0;
#endif
#ifdef PS5_MEDIA_AUDIO
    if (models[selected_model].architecture == RuntimeArchitecture::StableAudio ||
        models[selected_model].architecture == RuntimeArchitecture::PocketTTS ||
        models[selected_model].architecture == RuntimeArchitecture::KokoroTTS)
        return 0;
#ifdef PS5_MEDIA_IMAGE
    if (!ps5SdReleaseDirectArenaIfEmpty())
        return 2;
#endif
    if (ps5_agc_backend_release_scratch() != 0)
        return 2;
#endif

    const ps5_chat_message_t message{"user", "Hello"};
#ifdef PS5_QWEN_SMOKE_TEST
    const int result = run_model_chat(&message, 1, 32, smoke_progress);
#else
    const int result = run_model_chat(&message, 1, 1, nullptr);
#endif
    char line[384]{};
    std::snprintf(line, sizeof(line),
                  "[prosperogpt] warmup rc=%08X stage=%u prompt=%u generated=%u "
                  "resident=%u load_us=%llu prefill_us=%llu elapsed_us=%llu "
                  "first_token=%u tok_stage=%u tok_detail=%llu\n",
                  static_cast<unsigned>(result), ps5_compute_stage, ps5_compute_prompt_count,
                  ps5_compute_generated_count, ps5_compute_model_reused,
                  static_cast<unsigned long long>(ps5_compute_load_us),
                  static_cast<unsigned long long>(ps5_compute_prefill_us),
                  static_cast<unsigned long long>(ps5_compute_elapsed_us),
                  ps5_compute_generated_count ? ps5_compute_generated_tokens[0] : UINT32_MAX,
                  ps5_tokenizer_load_stage,
                  static_cast<unsigned long long>(ps5_tokenizer_load_detail));
    sceKernelDebugOutText(0, line);
#ifdef PS5_GPU_TIMING
    log_gpu_timing();
#endif
#if defined(PS5_QWEN_DIAGNOSTIC_PHASES) || defined(PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES)
    char state_line[384]{};
    std::snprintf(state_line, sizeof(state_line),
                  "[prosperogpt] qwen_phase_state=%08X,%08X,%08X,%08X,%08X,%08X,"
                  "%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X\n",
                  ps5_compute_debug_state_bits[0], ps5_compute_debug_state_bits[1],
                  ps5_compute_debug_state_bits[2], ps5_compute_debug_state_bits[3],
                  ps5_compute_debug_state_bits[4], ps5_compute_debug_state_bits[5],
                  ps5_compute_debug_state_bits[6], ps5_compute_debug_state_bits[7],
                  ps5_compute_debug_state_bits[8], ps5_compute_debug_state_bits[9],
                  ps5_compute_debug_state_bits[10], ps5_compute_debug_state_bits[11],
                  ps5_compute_debug_state_bits[12], ps5_compute_debug_state_bits[13],
                  ps5_compute_debug_state_bits[14], ps5_compute_debug_state_bits[15]);
    sceKernelDebugOutText(0, state_line);
#endif
#ifdef PS5_QWEN_SMOKE_TEST
    char token_line[256]{};
    std::snprintf(token_line, sizeof(token_line), "[prosperogpt] qwen_smoke_tokens=%u,%u,%u,%u\n",
                  ps5_compute_generated_count > 0 ? ps5_compute_generated_tokens[0] : UINT32_MAX,
                  ps5_compute_generated_count > 1 ? ps5_compute_generated_tokens[1] : UINT32_MAX,
                  ps5_compute_generated_count > 2 ? ps5_compute_generated_tokens[2] : UINT32_MAX,
                  ps5_compute_generated_count > 3 ? ps5_compute_generated_tokens[3] : UINT32_MAX);
    sceKernelDebugOutText(0, token_line);
    char response_line[4352]{};
    std::snprintf(response_line, sizeof(response_line), "[prosperogpt] qwen_smoke_response=%s\n",
                  ps5_compute_response);
    sceKernelDebugOutText(0, response_line);
#endif
#ifdef PS5_DUAL_BACKEND_SMOKE_SWITCH
    int switch_result = 1;
    int restore_result = 1;
    unsigned other = model_count;
    for (unsigned index = 0; index < model_count; ++index)
    {
        if (models[index].architecture != models[selected_model].architecture &&
            backend_for(models[index].architecture))
        {
            other = index;
            break;
        }
    }
    const unsigned initial = selected_model;
    if (other < model_count && gpt_runtime_select_model(other))
    {
        switch_result = run_model_chat(&message, 1, 32, nullptr);
        char switch_line[4352]{};
        std::snprintf(switch_line, sizeof(switch_line),
                      "[prosperogpt] architecture_switch model=%s rc=%08X "
                      "prompt=%u generated=%u load_us=%llu first_token=%u response=%s\n",
                      models[other].name, static_cast<unsigned>(switch_result),
                      ps5_compute_prompt_count, ps5_compute_generated_count,
                      static_cast<unsigned long long>(ps5_compute_load_us),
                      ps5_compute_generated_count ? ps5_compute_generated_tokens[0] : UINT32_MAX,
                      ps5_compute_response);
        sceKernelDebugOutText(0, switch_line);
    }
    if (gpt_runtime_select_model(initial))
    {
        restore_result = run_model_chat(&message, 1, 1, nullptr);
        char restore_line[384]{};
        std::snprintf(restore_line, sizeof(restore_line),
                      "[prosperogpt] architecture_restore model=%s rc=%08X "
                      "prompt=%u generated=%u load_us=%llu first_token=%u\n",
                      models[initial].name, static_cast<unsigned>(restore_result),
                      ps5_compute_prompt_count, ps5_compute_generated_count,
                      static_cast<unsigned long long>(ps5_compute_load_us),
                      ps5_compute_generated_count ? ps5_compute_generated_tokens[0] : UINT32_MAX);
        sceKernelDebugOutText(0, restore_line);
    }
    if (switch_result != 0 || restore_result != 0)
        return switch_result ? switch_result : restore_result;
#endif
    return result;
}

int gpt_runtime_generate(const gpt_runtime_message_t *messages, unsigned message_count,
                         const gpt_runtime_settings_t &settings, char *output,
                         std::size_t output_capacity, gpt_runtime_stats_t *stats,
                         gpt_runtime_progress_fn progress)
{
    if (!messages || message_count < 2 || !output || !output_capacity)
        return 1;
    load_models();
    if (!model_count)
    {
        std::snprintf(output, output_capacity,
                      "No compatible model is installed in the models folder.");
        return 1;
    }

#ifdef PS5_MEDIA_IMAGE
    if (models[selected_model].architecture == RuntimeArchitecture::StableDiffusion)
    {
        const char *prompt = nullptr;
        for (unsigned index = message_count; index > 0; --index)
            if (messages[index - 1].role && messages[index - 1].content &&
                std::strcmp(messages[index - 1].role, "user") == 0)
            {
                prompt = messages[index - 1].content;
                break;
            }
        std::uint64_t elapsed = 0;
        const int result = ps5_sd_generate(models[selected_model].root, prompt, output,
                                           output_capacity, &elapsed, progress);
        if (stats)
        {
            *stats = {};
            stats->generated_tokens = result == 0 ? 1 : 0;
            stats->elapsed_microseconds = elapsed;
        }
        return result;
    }
#endif

#ifdef PS5_MEDIA_AUDIO
    if (models[selected_model].architecture == RuntimeArchitecture::StableAudio)
    {
        const char *prompt = nullptr;
        for (unsigned index = message_count; index > 0; --index)
            if (messages[index - 1].role && messages[index - 1].content &&
                std::strcmp(messages[index - 1].role, "user") == 0)
            {
                prompt = messages[index - 1].content;
                break;
            }
        std::uint64_t elapsed = 0;
        const int result =
            ps5_stable_audio_generate(prompt, output, output_capacity, &elapsed, progress);
        if (stats)
        {
            *stats = {};
            stats->generated_tokens = result == 0 ? 1 : 0;
            stats->elapsed_microseconds = elapsed;
        }
        return result;
    }
    if (models[selected_model].architecture == RuntimeArchitecture::PocketTTS)
    {
        const char *prompt = nullptr;
        for (unsigned index = message_count; index > 0; --index)
            if (messages[index - 1].role && messages[index - 1].content &&
                std::strcmp(messages[index - 1].role, "user") == 0)
            {
                prompt = messages[index - 1].content;
                break;
            }
        std::uint64_t elapsed = 0;
        const int result = ps5_pocket_tts_generate(models[selected_model].root, prompt, output,
                                                   output_capacity, &elapsed, progress);
        if (stats)
        {
            *stats = {};
            stats->generated_tokens = result == 0 ? 1 : 0;
            stats->elapsed_microseconds = elapsed;
        }
        return result;
    }
    if (models[selected_model].architecture == RuntimeArchitecture::KokoroTTS)
    {
        const char *prompt = nullptr;
        for (unsigned index = message_count; index > 0; --index)
            if (messages[index - 1].role && messages[index - 1].content &&
                std::strcmp(messages[index - 1].role, "user") == 0)
            {
                prompt = messages[index - 1].content;
                break;
            }
        std::uint64_t elapsed = 0;
        const int result = ps5_kokoro_tts_generate(models[selected_model].root, prompt, output,
                                                   output_capacity, &elapsed, progress);
        if (stats)
        {
            *stats = {};
            stats->generated_tokens = result == 0 ? 1 : 0;
            stats->elapsed_microseconds = elapsed;
        }
        return result;
    }
#endif

    // Mistral has no separate system role; Qwen keeps the role verbatim.
    const char *system = nullptr;
    unsigned source = 0;
    const bool mistral = models[selected_model].architecture == RuntimeArchitecture::Mistral7B;
    if (mistral && messages[0].role && std::strcmp(messages[0].role, "system") == 0)
    {
        system = messages[0].content;
        source = 1;
    }

    constexpr unsigned kMaximumConversationMessages = 64;
    ps5_chat_message_t adapted[kMaximumConversationMessages + 1]{};
    char first_user[4096]{};
    unsigned count = 0;
    for (; source < message_count && count < kMaximumConversationMessages + 1; ++source, ++count)
    {
        adapted[count].role = messages[source].role;
        adapted[count].content = messages[source].content;
        if (count == 0 && system && messages[source].content)
        {
            std::snprintf(first_user, sizeof(first_user), "%s\n\n%s", system,
                          messages[source].content);
            adapted[count].content = first_user;
        }
    }
    if (source != message_count || count == 0)
        return 1;

    const int result = run_model_chat(adapted, count, settings.max_output_tokens, progress);
    char line[256]{};
    std::snprintf(line, sizeof(line),
                  "[prosperogpt] gpu_stats rc=%08X stage=%u prompt=%u generated=%u "
                  "resident=%u kv_reused=%u load_us=%llu prefill_us=%llu elapsed_us=%llu\n",
                  static_cast<unsigned>(result), ps5_compute_stage, ps5_compute_prompt_count,
                  ps5_compute_generated_count, ps5_compute_model_reused, ps5_compute_kv_reused,
                  static_cast<unsigned long long>(ps5_compute_load_us),
                  static_cast<unsigned long long>(ps5_compute_prefill_us),
                  static_cast<unsigned long long>(ps5_compute_elapsed_us));
    sceKernelDebugOutText(0, line);
#ifdef PS5_GPU_TIMING
    log_gpu_timing();
#endif
    if (stats)
    {
        stats->prompt_tokens = ps5_compute_prompt_count;
        stats->generated_tokens = ps5_compute_generated_count;
        stats->reused_tokens = ps5_compute_kv_reused;
        stats->prefill_microseconds = ps5_compute_prefill_us;
        stats->elapsed_microseconds = ps5_compute_elapsed_us;
    }
    if (result == 0)
    {
        std::snprintf(output, output_capacity, "%s", ps5_compute_response);
    }
    else if (gpt_runtime_context_full())
    {
        std::snprintf(output, output_capacity,
                      "This message is too long for the 4,096-token context.");
    }
    else if (!ps5_compute_tokenizer_path)
    {
        std::snprintf(output, output_capacity, "The selected model's tokenizer.ps5tok is missing.");
    }
    else if (!ps5_compute_model_path)
    {
        std::snprintf(output, output_capacity, "The selected model's model.ps5lm is missing.");
    }
    return result;
}
