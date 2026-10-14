#include "runtime_residency.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// Stable Audio 3 through audio.cpp's C API and the shared ggml/Vulkan runtime.
#include "audiocpp.h"
#include "../../../include/generated_output_path.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C"
{
int sceKernelDebugOutText(int, const char *);
std::uint64_t sceKernelGetProcessTime(void);
}

namespace
{
constexpr char kModelFile[] = "stable-audio-3-small-sfx-q8_0.gguf";
audiocpp_registry *g_registry;
audiocpp_model *g_model;
audiocpp_options *g_options;
audiocpp_session *g_session;
std::string g_model_path;

void release_engine()
{
    prospero_runtime::set_model_resident(false);
    audiocpp_session_free(g_session);
    g_session = nullptr;
    audiocpp_options_free(g_options);
    g_options = nullptr;
    audiocpp_model_free(g_model);
    g_model = nullptr;
    audiocpp_registry_free(g_registry);
    g_registry = nullptr;
    g_model_path.clear();
}

void write_u16(std::FILE *file, std::uint16_t value)
{
    const unsigned char bytes[] = {static_cast<unsigned char>(value & 0xff),
                                   static_cast<unsigned char>((value >> 8) & 0xff)};
    std::fwrite(bytes, 1, sizeof(bytes), file);
}

void write_u32(std::FILE *file, std::uint32_t value)
{
    write_u16(file, static_cast<std::uint16_t>(value));
    write_u16(file, static_cast<std::uint16_t>(value >> 16));
}

bool write_wav(const char *path, const float *samples, std::size_t frames,
               int sample_rate, int channels)
{
    if (!samples || frames == 0 || sample_rate <= 0 || channels <= 0 || channels > 8 ||
        frames > (UINT32_MAX - 36u) / (static_cast<std::uint64_t>(channels) * 2u))
        return false;
    const auto sample_count = frames * static_cast<std::size_t>(channels);
    const auto data_bytes = static_cast<std::uint32_t>(sample_count * 2);
    std::FILE *file = std::fopen(path, "wbx");
    if (!file)
        return false;
    std::fwrite("RIFF", 1, 4, file);
    write_u32(file, 36u + data_bytes);
    std::fwrite("WAVEfmt ", 1, 8, file);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, static_cast<std::uint16_t>(channels));
    write_u32(file, static_cast<std::uint32_t>(sample_rate));
    write_u32(file, static_cast<std::uint32_t>(sample_rate * channels * 2));
    write_u16(file, static_cast<std::uint16_t>(channels * 2));
    write_u16(file, 16);
    std::fwrite("data", 1, 4, file);
    write_u32(file, data_bytes);
    for (std::size_t i = 0; i < sample_count; ++i)
    {
        float value = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        value = std::clamp(value, -1.0f, 1.0f);
        const auto pcm = static_cast<std::int16_t>(value < 0 ? value * 32768.0f
                                                             : value * 32767.0f);
        write_u16(file, static_cast<std::uint16_t>(pcm));
    }
    const bool complete = std::ferror(file) == 0;
    return std::fclose(file) == 0 && complete;
}

void log_error(const char *stage)
{
    char line[512];
    std::snprintf(line, sizeof(line), "[prosperoai:stable-audio] %s: %.420s\n",
                  stage, audiocpp_last_error());
    sceKernelDebugOutText(0, line);
}

bool ensure_engine(const std::string &model_path, void (*progress)(const char *))
{
    if (g_model && g_session && g_model_path == model_path)
        return true;
    release_engine();
    const audiocpp_model_config model_config = {"stable_audio", nullptr, nullptr, nullptr};
    if (audiocpp_registry_create(nullptr, &g_registry) != AUDIOCPP_OK)
    {
        log_error("registry creation failed");
        release_engine();
        return false;
    }
    if (progress)
        progress("Loading Stable Audio 3 model");
    if (audiocpp_model_load(g_registry, model_path.c_str(), &model_config, nullptr, &g_model) != AUDIOCPP_OK)
    {
        log_error("model load failed");
        release_engine();
        return false;
    }
    g_options = audiocpp_options_create();
    if (!g_options || audiocpp_options_set(g_options, "stable_audio.mem_saver", "true") != AUDIOCPP_OK)
    {
        log_error("session options creation failed");
        release_engine();
        return false;
    }
    const audiocpp_backend_config backend_config = {"vulkan", 0, 4};
    if (audiocpp_session_create(g_model, "gen", "offline", &backend_config,
                                g_options, &g_session) != AUDIOCPP_OK)
    {
        log_error("Vulkan session creation failed");
        release_engine();
        return false;
    }
    g_model_path = model_path;
    prospero_runtime::set_model_resident(true);
    sceKernelDebugOutText(0, "[prosperoai:stable-audio] Vulkan model ready\n");
    return true;
}
} // namespace

extern "C" void ps5_stable_audio_release()
{
    release_engine();
}

extern "C" int ps5_stable_audio_generate(const char *model_root, const char *prompt,
                                         char *response, std::size_t response_capacity,
                                         std::uint64_t *elapsed_microseconds,
                                         void (*progress)(const char *))
{
    if (!model_root || !*model_root || !prompt || !*prompt || !response || !response_capacity)
        return 1;
    char output[160];
    if (!prospero_generated_output_path(output, sizeof(output), "audio", "stable-audio-3", "wav"))
        return 1;
    if (progress)
        progress("Loading Stable Audio 3 on Vulkan");

    const std::string model_path = std::string(model_root) + "/" + kModelFile;
    audiocpp_request *request = nullptr;
    audiocpp_result *result = nullptr;
    int result_code = 0;
    const std::uint64_t start = sceKernelGetProcessTime();

    do
    {
        if (!ensure_engine(model_path, progress))
        {
            result_code = 2;
            break;
        }
        request = audiocpp_request_create();
        if (!request || audiocpp_request_set_text(request, prompt, "en") != AUDIOCPP_OK ||
            audiocpp_request_set_option(request, "duration_sec", "4") != AUDIOCPP_OK ||
            audiocpp_request_set_option(request, "num_inference_steps", "8") != AUDIOCPP_OK ||
            audiocpp_request_set_option(request, "seed", "42") != AUDIOCPP_OK)
        {
            log_error("request setup failed");
            result_code = 5;
            break;
        }
        sceKernelDebugOutText(0, "[prosperoai:stable-audio] generation begin\n");
        if (progress)
            progress("Generating audio with ggml Vulkan");
        if (audiocpp_session_run(g_session, request, &result) != AUDIOCPP_OK)
        {
            log_error("Vulkan generation failed");
            result_code = 6;
            break;
        }
        const float *samples = nullptr;
        std::size_t frames = 0;
        int sample_rate = 0, channels = 0;
        if (audiocpp_result_audio(result, &samples, &frames, &sample_rate, &channels) != AUDIOCPP_OK ||
            !samples || !frames || !write_wav(output, samples, frames, sample_rate, channels))
        {
            log_error("WAV output failed");
            std::remove(output);
            result_code = 7;
            break;
        }
        sceKernelDebugOutText(0, "[prosperoai:stable-audio] generation complete\n");
        std::snprintf(response, response_capacity, "Audio generated locally at %s", output);
    } while (false);

    audiocpp_result_free(result);
    sceKernelDebugOutText(0, "[prosperoai:stable-audio] result released\n");
    audiocpp_request_free(request);
    sceKernelDebugOutText(0, "[prosperoai:stable-audio] request released\n");
    if (result_code != 0)
        release_engine();
    sceKernelDebugOutText(0, "[prosperoai:stable-audio] adapter return\n");
    if (elapsed_microseconds)
        *elapsed_microseconds = sceKernelGetProcessTime() - start;
    return result_code;
}
