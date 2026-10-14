#include "runtime_residency.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// Speech through kokoro.cpp on the shared ggml/Vulkan backend. This replaces the prebuilt
// OpenCL archive, whose wall time sat in its own CPU code rather than in any kernel the PS5
// OpenCL shim could forward to the GPU.
#include "kokoro.h"
#include "generated_voice_catalog.hpp"
#include "runtime_voice_catalog.hpp"
#include "ggml-vulkan.h"
#include "storage.hpp"
#include "../../../include/generated_output_path.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C"
{
    int sceKernelDebugOutText(int, const char *);
    std::uint64_t sceKernelGetProcessTime(void);
}

namespace
{
// The voice pack that ships with the model folder. Other packs are additional GGUF files.
constexpr char kModelFile[] = "kokoro-82m-f16.gguf";
std::string voice_file(const char *voice)
{
    if (voice)
        for (std::size_t i = 0; i < prospero_voice::kokoro_count; ++i)
            if (std::strcmp(voice, prospero_voice::kokoro[i].id) == 0)
                return prospero_voice::kokoro[i].filename;
    std::string stem;
    if (prospero_voice::safe_voice_stem(voice, stem)) return stem + ".gguf";
    return prospero_voice::kokoro[0].filename;
}

void log_line(const char *text)
{
    char line[512];
    std::snprintf(line, sizeof(line), "[prosperoai] %s", text);
    sceKernelDebugOutText(0, line);
}

void forward_log(enum kokoro_log_level, const char *message, void *)
{
    if (!message)
        return;
    char line[512];
    std::snprintf(line, sizeof(line), "[prosperoai] kokoro %s\n", message);
    sceKernelDebugOutText(0, line);
}

std::int16_t to_pcm16(float value)
{
    const float scaled = value * 32767.0f;
    if (scaled > 32767.0f)
        return 32767;
    if (scaled < -32768.0f)
        return -32768;
    return static_cast<std::int16_t>(scaled < 0 ? scaled - 0.5f : scaled + 0.5f);
}

void write_u32(std::FILE *file, std::uint32_t value)
{
    const unsigned char bytes[4] = {static_cast<unsigned char>(value & 0xFF),
                                    static_cast<unsigned char>((value >> 8) & 0xFF),
                                    static_cast<unsigned char>((value >> 16) & 0xFF),
                                    static_cast<unsigned char>((value >> 24) & 0xFF)};
    std::fwrite(bytes, 1, sizeof(bytes), file);
}

void write_u16(std::FILE *file, std::uint16_t value)
{
    const unsigned char bytes[2] = {static_cast<unsigned char>(value & 0xFF),
                                    static_cast<unsigned char>((value >> 8) & 0xFF)};
    std::fwrite(bytes, 1, sizeof(bytes), file);
}

// 16-bit mono PCM, which is what ps5_media_play_wav and the web client already read.
bool write_wav(const char *path, const float *samples, int count, int rate)
{
    if (count <= 0 || rate <= 0)
        return false;
    std::FILE *file = std::fopen(path, "wbx");
    if (!file)
        return false;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(count) * 2u;
    std::fwrite("RIFF", 1, 4, file);
    write_u32(file, 36u + data_bytes);
    std::fwrite("WAVEfmt ", 1, 8, file);
    write_u32(file, 16u);
    write_u16(file, 1u); // PCM
    write_u16(file, 1u); // mono
    write_u32(file, static_cast<std::uint32_t>(rate));
    write_u32(file, static_cast<std::uint32_t>(rate) * 2u);
    write_u16(file, 2u);
    write_u16(file, 16u);
    std::fwrite("data", 1, 4, file);
    write_u32(file, data_bytes);
    for (int i = 0; i < count; ++i)
    {
        const std::int16_t value = to_pcm16(samples[i]);
        write_u16(file, static_cast<std::uint16_t>(value));
    }
    const bool complete = std::ferror(file) == 0;
    return std::fclose(file) == 0 && complete;
}
} // namespace

extern "C" int ps5_kokoro_tts_generate(const char *root, const char *voice_id, const char *prompt, char *response,
                                       std::size_t response_capacity,
                                       std::uint64_t *elapsed_microseconds,
                                       void (*progress)(const char *))
{
    if (!root || !*root || !prompt || !*prompt || !response || response_capacity == 0)
        return 1;
    char output[160];
    if (!prospero_generated_output_path(output, sizeof(output), "voice", "kokoro-speech", "wav"))
        return 1;
    if (progress)
        progress("Synthesizing speech on the GPU");
    kokoro_log_set(forward_log, nullptr);

    const std::string base(root);
    const std::string model = base + "/" + kModelFile;
    const std::string voice_filename = voice_file(voice_id);
    std::string voice = prospero::storage().data_root + "/voices/kokoro/" + voice_filename;
    std::FILE *voice_check = std::fopen(voice.c_str(), "rb");
    if (!voice_check)
        voice = base + "/" + voice_filename;
    else
        std::fclose(voice_check);
    // espeak-ng reads its dictionaries from the model folder's assets, not from a system path.
    const std::string espeak_data = base + "/assets";
    setenv("KOKORO_ESPEAK_DATA_PATH", espeak_data.c_str(), 1);
    // Keep the Vulkan TTS stages serial on PS5. The harmonic source is CPU work,
    // but running it concurrently with GGML's Vulkan scheduler is not safe on
    // this console's single shared inference device.
    setenv("KOKORO_HAR_OVERLAP", "0", 1);

    // The speech model needs room on the Vulkan heap; say how much there is before asking
    // for it, so a failure here is a number rather than a guess.
    std::size_t free_bytes = 0, total_bytes = 0;
    ggml_backend_vk_get_device_memory(0, &free_bytes, &total_bytes);
    char memory_line[160];
    std::snprintf(memory_line, sizeof(memory_line), "kokoro_memory free=%zu MiB total=%zu MiB\n",
                  free_bytes >> 20, total_bytes >> 20);
    log_line(memory_line);

    const std::uint64_t start = sceKernelGetProcessTime();
    kokoro_context_params params = kokoro_context_default_params();
    params.backend = KOKORO_BACKEND_VULKAN;
    params.n_threads = 4;
    kokoro_context *context = kokoro_init_from_file(model.c_str(), params);
    prospero_runtime::set_model_resident(context != nullptr);
    if (!context)
    {
        std::snprintf(response, response_capacity, "Could not load the speech model: %s",
                      kokoro_last_error());
        return 2;
    }
    if (kokoro_load_voice_pack(context, voice.c_str()) != KOKORO_STATUS_OK)
    {
        std::snprintf(response, response_capacity, "Could not load the voice pack: %s",
                      kokoro_last_error());
        kokoro_free(context);
        prospero_runtime::set_model_resident(false);
        return 3;
    }

    kokoro_audio audio = {};
    const enum kokoro_status status = kokoro_synthesize_audio(context, prompt, &audio);
    const std::uint64_t elapsed = sceKernelGetProcessTime() - start;
    if (elapsed_microseconds)
        *elapsed_microseconds = elapsed;
    if (status != KOKORO_STATUS_OK || !audio.samples || audio.n_samples <= 0)
    {
        std::snprintf(response, response_capacity, "Speech synthesis failed: %s",
                      kokoro_last_error());
        kokoro_audio_free(&audio);
        kokoro_free(context);
        prospero_runtime::set_model_resident(false);
        return 4;
    }

    float peak = 0.0f;
    for (int i = 0; i < audio.n_samples; ++i)
    {
        const float magnitude = audio.samples[i] < 0 ? -audio.samples[i] : audio.samples[i];
        if (magnitude > peak)
            peak = magnitude;
    }
    char line[256];
    std::snprintf(line, sizeof(line),
                  "kokoro_output samples=%d rate=%d peak=%.4f backend=%s elapsed_us=%llu\n",
                  audio.n_samples, audio.sample_rate, static_cast<double>(peak),
                  kokoro_backend_name(context), static_cast<unsigned long long>(elapsed));
    log_line(line);

    const bool written = write_wav(output, audio.samples, audio.n_samples, audio.sample_rate);
    kokoro_audio_free(&audio);
    kokoro_free(context);
        prospero_runtime::set_model_resident(false);
    if (!written)
    {
        std::remove(output);
        std::snprintf(response, response_capacity, "Could not write %s", output);
        return 5;
    }
    std::snprintf(response, response_capacity, "Speech generated locally: %s", output);
    return 0;
}
