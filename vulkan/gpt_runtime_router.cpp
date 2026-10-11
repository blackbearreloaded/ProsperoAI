// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef PROSPERO_VULKAN_RUNTIME
#include "gpt_runtime.hpp"
#include "runtime_vulkan.hpp"
#ifdef PROSPERO_MEDIA_VULKAN
#include "media_vulkan.hpp"
#endif
#include "model_paths.hpp"
#include "model_presets.hpp"
#include <mutex>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
extern "C"
{
    int ps5_sd_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                        gpt_runtime_progress_fn);
    int ps5_stable_audio_generate(const char *, char *, std::size_t, std::uint64_t *,
                                  gpt_runtime_progress_fn);
    int ps5_kokoro_tts_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                                gpt_runtime_progress_fn);
    void ps5_sd_shutdown();
    void ps5_media_stop();
    std::int64_t sceKernelGetDirectMemorySize();
    int sceKernelAvailableDirectMemorySize(std::int64_t, std::int64_t, std::size_t, std::int64_t *,
                                           std::size_t *);
    int sceKernelDebugOutText(int, const char *);
}
namespace
{
// Text and media share the pinned ggml/Vulkan backend.
enum Kind
{
    kVulkanText = 0, // a GGUF file, llama.cpp on Vulkan
    kImage = 1,
    kAudio = 2,
    kSpeech = 3,
};
bool is_media(int kind)
{
    return kind >= kImage && kind <= kSpeech;
}
struct Model
{
    std::string id, name, root;
    int kind;
    unsigned text_index; // in the shared Vulkan text runtime
};
std::vector<Model> models;
unsigned selected = 0;
bool scanned = false;
std::mutex mutex;
std::mutex inference_mutex;
const char *purposes[] = {"text-to-text", "text-to-image", "text-to-audio", "text-to-speech"};
bool exists(const std::string &path)
{
    struct stat st
    {
    };
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
void scan()
{
    if (scanned)
        return;
    scanned = true;
    for (unsigned i = 0, n = prospero_vulkan::gpt_runtime_model_count(); i < n; ++i)
        models.push_back({prospero_vulkan::gpt_runtime_model_id(i),
                          prospero_vulkan::gpt_runtime_model_name(i), "", kVulkanText, i});
    // Preserve installed legacy bundle IDs using the equivalent pinned GGUF. Never
    // read model.ps5lm into an inference backend or delete the user's old bundle.
    for (const auto &preset : prospero_model_download::presets)
    {
        const std::string id = preset.id;
        if (preset.kind || id.size() < 5 || id.substr(id.size() - 5) != ".gguf")
            continue;
        const std::string legacy = id.substr(0, id.size() - 5);
        if (!exists(std::string(prospero::model_root()) + "/" + legacy + "/model.ps5lm"))
            continue;
        const std::string source = preset.files[0].source;
        const std::string basename = source.substr(source.find_last_of('/') + 1);
        const auto found = std::find_if(models.begin(), models.end(), [&](const Model &m)
                                       { return m.id == id || m.id == basename; });
        if (found != models.end())
        {
            const unsigned index = found->text_index;
            models.push_back({legacy, std::string(preset.name) + " (GGUF)", "", kVulkanText, index});
        }
    }
    for (const auto &preset : prospero_model_download::presets)
    {
        if (!preset.kind)
            continue;
        const std::string root = std::string(prospero::model_root()) + "/" + preset.id;
        bool complete = true;
        for (std::size_t i = 0; i < preset.count; ++i)
        {
            const std::string path = preset.files[i].path;
            if (path.compare(0, 9, "licenses/") != 0 && !exists(root + "/" + path))
            {
                complete = false;
                break;
            }
        }
        if (complete)
            models.push_back({preset.id, preset.name, root, preset.kind, 0});
    }
}
// Written at every change of model, after the one in memory was let go: with one
// model in memory at a time, the largest free block comes back to what it was.
void log_change(const Model &from, const Model &to)
{
    std::int64_t start = 0;
    std::size_t free_bytes = 0;
    sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 0x4000, &start,
                                       &free_bytes);
    char line[256];
    std::snprintf(line, sizeof(line),
                  "[prosperoai] model change: %s -> %s, largest free block %zu MiB\n",
                  from.id.c_str(), to.id.c_str(), free_bytes >> 20);
    sceKernelDebugOutText(0, line);
}
void stop_media()
{
#ifdef PROSPERO_MEDIA_VULKAN
    prospero_media_vk::release();
#endif
    ps5_media_stop();
    ps5_sd_shutdown();
}
} // namespace
bool gpt_runtime_available()
{
    return gpt_runtime_model_count() != 0;
}
unsigned gpt_runtime_model_count()
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    return models.size();
}
unsigned gpt_runtime_selected_model()
{
    std::lock_guard<std::mutex> lock(mutex);
    return selected;
}
const char *gpt_runtime_model_id(unsigned i)
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    thread_local std::string id;
    id = i < models.size() ? models[i].id : "";
    return id.c_str();
}
const char *gpt_runtime_model_name(unsigned i)
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    thread_local std::string name;
    name = i < models.size() ? models[i].name : "";
    return name.c_str();
}
const char *gpt_runtime_model_purpose(unsigned i)
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    return i < models.size() ? purposes[models[i].kind] : "unknown";
}
const char *gpt_runtime_name()
{
    return gpt_runtime_model_name(gpt_runtime_selected_model());
}
const char *gpt_runtime_purpose()
{
    return gpt_runtime_model_purpose(gpt_runtime_selected_model());
}
const char *gpt_runtime_backend()
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    if (selected < models.size() && is_media(models[selected].kind))
        return "Vulkan · Media";
    return prospero_vulkan::gpt_runtime_backend();
}
bool gpt_runtime_context_full()
{
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    if (selected >= models.size())
        return false;
    return models[selected].kind == kVulkanText && prospero_vulkan::gpt_runtime_context_full();
}
namespace
{
bool select_model_locked(unsigned index)
{
    scan();
    if (index >= models.size())
        return false;
    if (index == selected)
        return true;
    // One model in memory: what the other runtimes hold goes before the next is loaded.
    const int from = models[selected].kind;
    const int to = models[index].kind;
    if (is_media(from))
        stop_media();
    if (to != kVulkanText)
        prospero_vulkan::release_model_memory();
    log_change(models[selected], models[index]);
    if (to == kVulkanText && !prospero_vulkan::gpt_runtime_select_model(models[index].text_index))
        return false;
    selected = index;
    return true;
}
} // namespace
bool gpt_runtime_select_model(unsigned index)
{
    std::unique_lock<std::mutex> inference(inference_mutex, std::try_to_lock);
    if (!inference.owns_lock())
        return false;
    std::lock_guard<std::mutex> lock(mutex);
    return select_model_locked(index);
}
void gpt_runtime_refresh_models()
{
    std::unique_lock<std::mutex> inference(inference_mutex, std::try_to_lock);
    if (!inference.owns_lock())
        return;
    std::lock_guard<std::mutex> lock(mutex);
    scan();
    const std::string id = selected < models.size() ? models[selected].id : "";
    prospero_vulkan::gpt_runtime_refresh_models();
    models.clear();
    scanned = false;
    scan();
    const auto found =
        std::find_if(models.begin(), models.end(), [&](const Model &m) { return m.id == id; });
    selected = found == models.end() ? 0 : found - models.begin();
}
int gpt_runtime_prepare()
{
    std::unique_lock<std::mutex> inference(inference_mutex, std::try_to_lock);
    if (!inference.owns_lock())
        return 1;
    int kind;
    {
        std::lock_guard<std::mutex> lock(mutex);
        scan();
        if (selected >= models.size())
            return 1;
        kind = models[selected].kind;
    }
    if (kind == kVulkanText)
        return prospero_vulkan::gpt_runtime_prepare();
    return 0;
}
int gpt_runtime_generate(const gpt_runtime_message_t *messages, unsigned count,
                         const gpt_runtime_settings_t &settings, char *output, std::size_t capacity,
                         gpt_runtime_stats_t *stats, gpt_runtime_progress_fn progress)
{
    if (!messages || !count || !output || !capacity)
        return 1;
    std::unique_lock<std::mutex> inference(inference_mutex, std::try_to_lock);
    if (!inference.owns_lock())
    {
        std::snprintf(output, capacity, "Another generation is running. Please wait.");
        return 1;
    }
    Model model;
    {
        std::lock_guard<std::mutex> lock(mutex);
        scan();
        if (selected >= models.size())
            return 1;
        if (settings.model_id && *settings.model_id)
        {
            const auto found = std::find_if(models.begin(), models.end(), [&](const Model &m)
                                            { return m.id == settings.model_id; });
            if (found == models.end() || !select_model_locked(found - models.begin()))
                return 1;
        }
        model = models[selected];
    }
    // Inference owns its backend, but does not hold the catalog lock. HTTP model
    // lists and the native UI remain responsive while media generation runs.
    if (model.kind == kVulkanText)
    {
        auto resolved = settings;
        const std::string actual = prospero_vulkan::gpt_runtime_model_id(model.text_index);
        resolved.model_id = actual.c_str();
        return prospero_vulkan::gpt_runtime_generate(messages, count, resolved, output, capacity,
                                                     stats, progress);
    }
    const char *prompt = nullptr;
    for (unsigned i = count; i > 0; --i)
        if (messages[i - 1].role && messages[i - 1].content &&
            std::string(messages[i - 1].role) == "user")
        {
            prompt = messages[i - 1].content;
            break;
        }
    if (!prompt || !*prompt)
        return 1;
    std::uint64_t elapsed = 0;
    int result =
        model.kind == 1
            ? ps5_sd_generate(model.root.c_str(), prompt, output, capacity, &elapsed, progress)
        : model.kind == 2 ? ps5_stable_audio_generate(prompt, output, capacity, &elapsed, progress)
                          : ps5_kokoro_tts_generate(model.root.c_str(), prompt, output, capacity,
                                                    &elapsed, progress);
#ifdef PROSPERO_MEDIA_VULKAN
    // Do not keep media Vulkan buffers or backend state across a switch to text.
    prospero_media_vk::print_stats();
    prospero_media_vk::release();
#endif
    if (stats)
    {
        *stats = {};
        stats->generated_tokens = result == 0 ? 1 : 0;
        stats->elapsed_microseconds = elapsed;
    }
    return result;
}
#endif
