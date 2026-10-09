// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime_vulkan.hpp"
#include "model_paths.hpp"
#include "model_presets.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>
namespace
{
unsigned text_selected = 0, released = 0, stopped = 0, generated = 0;
std::atomic<bool> block_voice{false}, voice_entered{false}, voice_release{false};
} // namespace
namespace prospero_vulkan
{
unsigned gpt_runtime_model_count()
{
    return 2;
}
const char *gpt_runtime_model_id(unsigned i)
{
    return i ? "text1" : "text0";
}
const char *gpt_runtime_model_name(unsigned i)
{
    return gpt_runtime_model_id(i);
}
const char *gpt_runtime_backend()
{
    return "Vulkan";
}
bool gpt_runtime_context_full()
{
    return true;
}
bool gpt_runtime_select_model(unsigned i)
{
    text_selected = i;
    return i < 2;
}
void release_model_memory()
{
    ++released;
}
void gpt_runtime_refresh_models()
{
}
int gpt_runtime_prepare()
{
    return 0;
}
int gpt_runtime_generate(const gpt_runtime_message_t *, unsigned, const gpt_runtime_settings_t &,
                         char *out, std::size_t n, gpt_runtime_stats_t *, gpt_runtime_progress_fn)
{
    std::snprintf(out, n, "text%u", text_selected);
    return 0;
}
} // namespace prospero_vulkan
extern "C"
{
    void ps5_media_stop()
    {
        ++stopped;
    }
    void ps5_sd_shutdown()
    {
    }
    int ps5_agc_backend_release_scratch()
    {
        return 0;
    }
    int ps5_sd_generate(const char *, const char *, char *, std::size_t, std::uint64_t *,
                        gpt_runtime_progress_fn)
    {
        return 1;
    }
    int ps5_stable_audio_generate(const char *, char *, std::size_t, std::uint64_t *,
                                  gpt_runtime_progress_fn)
    {
        return 1;
    }
    int ps5_kokoro_tts_generate(const char *root, const char *prompt, char *out, std::size_t n,
                                std::uint64_t *elapsed, gpt_runtime_progress_fn)
    {
        assert(std::string(root) == std::string(prospero::kModelRoot) + "/kokoro-82m-fp16");
        assert(std::strcmp(prompt, "Hello") == 0);
        ++generated;
        if (block_voice.load())
        {
            voice_entered.store(true);
            while (!voice_release.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        *elapsed = 123;
        std::snprintf(out, n, "voice.wav");
        return 0;
    }
}
int main()
{
    assert(gpt_runtime_model_count() == 2);
    assert(gpt_runtime_select_model(1));
    assert(text_selected == 1);
    const auto &voice = prospero_model_download::presets[4];
    for (std::size_t i = 0; i < voice.count; ++i)
    {
        const std::string relative = voice.files[i].path;
        if (relative.compare(0, 9, "licenses/") == 0)
            continue;
        const auto path = std::filesystem::path(prospero::kModelRoot) / voice.id / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path) << "fixture";
    }
    gpt_runtime_refresh_models();
    assert(gpt_runtime_model_count() == 3 && gpt_runtime_selected_model() == 1);
    assert(std::strcmp(gpt_runtime_model_purpose(2), "text-to-speech") == 0);
    assert(!gpt_runtime_select_model(3));
    assert(gpt_runtime_select_model(2) && released == 1);
    assert(gpt_runtime_prepare() == 0 && !gpt_runtime_context_full());
    const gpt_runtime_message_t messages[] = {{"user", "Hello"}};
    char out[100]{};
    gpt_runtime_stats_t stats{};
    assert(gpt_runtime_generate(messages, 1, {}, out, sizeof(out), &stats, nullptr) == 0);
    assert(generated == 1 && stats.generated_tokens == 1 && stats.elapsed_microseconds == 123);
    assert(std::strcmp(out, "voice.wav") == 0);
    block_voice.store(true);
    gpt_runtime_settings_t voice_settings{};
    voice_settings.model_id = "kokoro-82m-fp16";
    std::thread worker(
        [&]
        {
            assert(gpt_runtime_generate(messages, 1, voice_settings, out, sizeof(out), &stats,
                                        nullptr) == 0);
        });
    for (int i = 0; i < 1000 && !voice_entered.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(voice_entered.load());
    assert(gpt_runtime_model_count() == 3);
    gpt_runtime_refresh_models();
    assert(!gpt_runtime_select_model(0));
    assert(std::strcmp(gpt_runtime_model_purpose(2), "text-to-speech") == 0);
    voice_release.store(true);
    worker.join();
    assert(gpt_runtime_select_model(0) && stopped == 1);
    assert(gpt_runtime_generate(messages, 1, {}, out, sizeof(out), &stats, nullptr) == 0);
    assert(std::strcmp(out, "text0") == 0);
    assert(gpt_runtime_model_count() == 3);
}
