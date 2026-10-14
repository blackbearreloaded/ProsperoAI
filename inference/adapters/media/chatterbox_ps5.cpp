#include "runtime_residency.hpp"
// SPDX-License-Identifier: GPL-3.0-or-later
// Chatterbox zero-shot voice cloning through audio.cpp's C API and the shared Vulkan backend.
#include "audiocpp.h"
#include "generated_voice_catalog.hpp"
#include "runtime_voice_catalog.hpp"
#include "chatterbox_languages.hpp"
#include "generated_output_path.hpp"
#include "storage.hpp"
#include <FLAC/stream_decoder.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
std::uint64_t sceKernelGetProcessTime(void);
int sceKernelDebugOutText(int, const char *);
std::int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAvailableDirectMemorySize(std::int64_t, std::int64_t, std::size_t, std::int64_t *, std::size_t *);
}

namespace {
constexpr const char *kLegacyCustomVoice = "/download0/ProsperoAI/voices/chatterbox/custom.wav";
std::unique_ptr<audiocpp_registry,decltype(&audiocpp_registry_free)> g_registry{nullptr,audiocpp_registry_free};
std::unique_ptr<audiocpp_model,decltype(&audiocpp_model_free)> g_model{nullptr,audiocpp_model_free};
std::unique_ptr<audiocpp_options,decltype(&audiocpp_options_free)> g_options{nullptr,audiocpp_options_free};
std::unique_ptr<audiocpp_session,decltype(&audiocpp_session_free)> g_session{nullptr,audiocpp_session_free};
std::string g_model_root;

void release_engine() {
    prospero_runtime::set_model_resident(false);
    g_session.reset(); g_options.reset(); g_model.reset(); g_registry.reset(); g_model_root.clear();
}

struct Audio {
    std::vector<float> samples;
    int rate = 0;
    int channels = 0;
};

FLAC__StreamDecoderWriteStatus decode_flac_frame(const FLAC__StreamDecoder *, const FLAC__Frame *frame,
                                                  const FLAC__int32 *const channels[], void *user_data) {
    auto &audio = *static_cast<Audio *>(user_data);
    const auto &header = frame->header;
    if ((header.channels != 1 && header.channels != 2) || header.sample_rate < 8000 ||
        header.sample_rate > 48000 || header.bits_per_sample < 16 || header.bits_per_sample > 24 ||
        (audio.rate && (audio.rate != static_cast<int>(header.sample_rate) ||
                        audio.channels != static_cast<int>(header.channels))) ||
        audio.samples.size() + static_cast<std::size_t>(header.blocksize) * header.channels >
            48000u * 12u * 2u) {
        return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    audio.rate = static_cast<int>(header.sample_rate);
    audio.channels = static_cast<int>(header.channels);
    const float scale = 1.0f / static_cast<float>(1u << (header.bits_per_sample - 1));
    for (unsigned frame_index = 0; frame_index < header.blocksize; ++frame_index) {
        for (unsigned channel = 0; channel < header.channels; ++channel) {
            audio.samples.push_back(static_cast<float>(channels[channel][frame_index]) * scale);
        }
    }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void ignore_flac_error(const FLAC__StreamDecoder *, FLAC__StreamDecoderErrorStatus, void *) {}

bool read_flac_reference(const char *path, Audio &audio) {
    FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
    if (!decoder) return false;
    const auto init = FLAC__stream_decoder_init_file(decoder, path, decode_flac_frame, nullptr,
                                                     ignore_flac_error, &audio);
    const bool ok = init == FLAC__STREAM_DECODER_INIT_STATUS_OK &&
                    FLAC__stream_decoder_process_until_end_of_stream(decoder) && !audio.samples.empty();
    FLAC__stream_decoder_finish(decoder);
    FLAC__stream_decoder_delete(decoder);
    return ok;
}

std::uint16_t u16(const unsigned char *p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}

std::uint32_t u32(const unsigned char *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool read_reference(const char *path, Audio &audio) {
    std::FILE *file = std::fopen(path, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) { std::fclose(file); return false; }
    const long end = std::ftell(file);
    if (end < 44 || end > 12 * 1024 * 1024 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file); return false;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(end));
    const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!read || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4))
        return bytes.size() >= 4 && !std::memcmp(bytes.data(), "fLaC", 4) && read_flac_reference(path, audio);
    unsigned format = 0, channels = 0, rate = 0, bits = 0;
    const unsigned char *pcm = nullptr;
    std::size_t pcm_size = 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::uint32_t size = u32(bytes.data() + at + 4);
        const std::size_t data_at = at + 8;
        if (size > bytes.size() - data_at) return false;
        if (!std::memcmp(bytes.data() + at, "fmt ", 4) && size >= 16) {
            format = u16(bytes.data() + data_at);
            channels = u16(bytes.data() + data_at + 2);
            rate = u32(bytes.data() + data_at + 4);
            bits = u16(bytes.data() + data_at + 14);
        } else if (!std::memcmp(bytes.data() + at, "data", 4)) {
            pcm = bytes.data() + data_at;
            pcm_size = size;
        }
        at = data_at + size + (size & 1u);
    }
    if (format != 1 || (channels != 1 && channels != 2) || rate < 8000 || rate > 48000 ||
        bits != 16 || !pcm || pcm_size == 0 || pcm_size % (channels * 2u)) return false;
    const std::size_t count = pcm_size / 2;
    audio.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = static_cast<std::int16_t>(u16(pcm + i * 2));
        audio.samples[i] = static_cast<float>(value) / 32768.0f;
    }
    audio.rate = static_cast<int>(rate);
    audio.channels = static_cast<int>(channels);
    return true;
}

void put_u16(std::FILE *f, std::uint16_t value) {
    const unsigned char b[] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8)};
    std::fwrite(b, 1, sizeof(b), f);
}
void put_u32(std::FILE *f, std::uint32_t value) {
    const unsigned char b[] = {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
                               static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
    std::fwrite(b, 1, sizeof(b), f);
}
bool write_wav(const char *path, const float *samples, std::size_t frames, int rate, int channels) {
    if (!samples || !frames || rate <= 0 || (channels != 1 && channels != 2) ||
        frames > (0xffffffffu - 36u) / (2u * static_cast<unsigned>(channels))) return false;
    std::FILE *f = std::fopen(path, "wbx");
    if (!f) return false;
    const std::uint32_t bytes = static_cast<std::uint32_t>(frames * channels * 2u);
    std::fwrite("RIFF", 1, 4, f); put_u32(f, 36u + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16); put_u16(f, 1); put_u16(f, static_cast<std::uint16_t>(channels));
    put_u32(f, static_cast<std::uint32_t>(rate)); put_u32(f, static_cast<std::uint32_t>(rate * channels * 2));
    put_u16(f, static_cast<std::uint16_t>(channels * 2)); put_u16(f, 16);
    std::fwrite("data", 1, 4, f); put_u32(f, bytes);
    for (std::size_t i = 0; i < frames * static_cast<std::size_t>(channels); ++i) {
        const float scaled = std::max(-32768.0f, std::min(32767.0f, samples[i] * 32767.0f));
        put_u16(f, static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled)));
    }
    const bool ok = std::ferror(f) == 0;
    return std::fclose(f) == 0 && ok;
}

void log_line(const char *message) {
    char line[384];
    std::snprintf(line, sizeof(line), "[prosperoai] chatterbox %s\n", message);
    sceKernelDebugOutText(0, line);
}

void log_failure(const char *stage) {
    char message[320];
    std::snprintf(message, sizeof(message), "%s: %.240s", stage, audiocpp_last_error());
    log_line(message);
}
struct MemoryLog {
    const char *stage;
    ~MemoryLog() {
        std::int64_t start = 0; std::size_t bytes = 0;
        const int rc = sceKernelAvailableDirectMemorySize(0, sceKernelGetDirectMemorySize(), 0x4000, &start, &bytes);
        char message[160];
        std::snprintf(message,sizeof(message),"%s largest free block=%zu MiB rc=0x%08x",stage,bytes>>20,static_cast<unsigned>(rc));
        log_line(message);
    }
};
} // namespace

extern "C" void ps5_chatterbox_release() { release_engine(); }

extern "C" int ps5_chatterbox_tts_generate(const char *root, const char *voice_id,
                                             const char *language, const char *prompt, char *response,
                                             std::size_t response_capacity,
                                             std::uint64_t *elapsed_microseconds,
                                             void (*progress)(const char *)) {
    if (!root || !*root || !voice_id || !prompt || !*prompt || !response || !response_capacity)
        return 1;
    const MemoryLog memory_after{"after request"};
    { const MemoryLog memory_before{"before request"}; }
    const auto voice = prospero_voice::runtime_voice_by_id(true, voice_id);
    if (voice.id.empty()) { std::snprintf(response, response_capacity, "Unknown Chatterbox voice."); return 1; }

    const bool custom_voice = voice.id == "custom";
    const std::string voice_root = prospero::storage().data_root + "/voices/chatterbox/";
    const std::string reference_path = voice_root + voice.filename;
    if (custom_voice) {
        struct stat info{};
        if (stat(reference_path.c_str(), &info) != 0)
            std::rename(kLegacyCustomVoice, reference_path.c_str());
    }
    Audio reference;
    const bool reference_loaded = read_reference(reference_path.c_str(), reference);
    if (!reference_loaded) {
        std::snprintf(response, response_capacity,
                      custom_voice
                          ? "Upload a WAV voice sample in the workspace first."
                          : "Download the Chatterbox voice files with the model first.");
        return 2;
    }

    if (progress) progress("Preparing Chatterbox voice");
    const std::uint64_t start = sceKernelGetProcessTime();
    // Inference is serialized by the router. Retain weights/conditionals between
    // requests; the adapter release hook tears down the session on model switches.
    if (!g_session || g_model_root!=root) {
        release_engine();
        audiocpp_registry *raw_registry = nullptr;
        if (audiocpp_registry_create(nullptr, &raw_registry) != AUDIOCPP_OK || !raw_registry) {
            log_failure("registry initialization failed");
            std::snprintf(response, response_capacity, "Could not initialize the Chatterbox runtime: %s",
                          audiocpp_last_error());
            return 3;
        }
        auto registry = std::unique_ptr<audiocpp_registry, decltype(&audiocpp_registry_free)>(
            raw_registry, audiocpp_registry_free);
        audiocpp_model_config config{"chatterbox", nullptr, nullptr, nullptr};
        audiocpp_model *raw_model = nullptr;
        if (audiocpp_model_load(raw_registry, root, &config, nullptr, &raw_model) != AUDIOCPP_OK || !raw_model) {
            log_failure("model load failed");
            std::snprintf(response, response_capacity, "Could not load Chatterbox: %s", audiocpp_last_error());
            return 4;
        }
        auto model = std::unique_ptr<audiocpp_model, decltype(&audiocpp_model_free)>(raw_model,
                                                                                           audiocpp_model_free);
        // Keep Chatterbox on the same shared Vulkan device as the other model
        // adapters. A CPU session tried to reserve a 192–256 MiB arena in the
        // title's constrained direct-memory heap and aborted the HTTP worker.
        audiocpp_backend_config backend{"vulkan", 0, 4};
        audiocpp_options *raw_session_options = audiocpp_options_create();
        if (!raw_session_options ||
            audiocpp_options_set(raw_session_options, "chatterbox.mem_saver", "true") != AUDIOCPP_OK) {
            log_failure("low-memory options failed");
            audiocpp_options_free(raw_session_options);
            std::snprintf(response, response_capacity, "Could not configure low-memory Chatterbox mode: %s",
                          audiocpp_last_error());
            return 5;
        }
        auto session_options = std::unique_ptr<audiocpp_options, decltype(&audiocpp_options_free)>(
            raw_session_options, audiocpp_options_free);
        audiocpp_session *raw_session = nullptr;
        log_line("session backend=vulkan device=0 mem_saver=true");
        if (audiocpp_session_create(raw_model, "clon", "offline", &backend, raw_session_options, &raw_session) != AUDIOCPP_OK || !raw_session) {
            log_failure("Vulkan session creation failed");
            std::snprintf(response, response_capacity, "Could not create Chatterbox session: %s", audiocpp_last_error());
            return 5;
        }
        auto session = std::unique_ptr<audiocpp_session, decltype(&audiocpp_session_free)>(raw_session,
                                                                                            audiocpp_session_free);
        g_registry=std::move(registry); g_model=std::move(model); g_options=std::move(session_options);
        g_session=std::move(session); g_model_root=root;
        prospero_runtime::set_model_resident(true);
    }
    audiocpp_request *raw_request = audiocpp_request_create();
    if (!raw_request) { std::snprintf(response, response_capacity, "Could not allocate a speech request."); return 6; }
    const auto request = std::unique_ptr<audiocpp_request, decltype(&audiocpp_request_free)>(raw_request,
                                                                                            audiocpp_request_free);
    // The spoken language is the user's choice; audio.cpp takes its multilingual tokenizer
    // path for anything other than "en". An unknown code would change the encoding silently,
    // so fall back to English rather than passing it through.
    const char *spoken = prospero_language::chatterbox_language_index(language) >= 0 ? language : "en";
    if (audiocpp_request_set_text(raw_request, prompt, spoken) != AUDIOCPP_OK ||
        audiocpp_request_set_voice_audio(raw_request, reference.samples.data(),
                                         reference.samples.size() / static_cast<std::size_t>(reference.channels),
                                         reference.rate, reference.channels) != AUDIOCPP_OK) {
        log_failure("voice request setup failed");
        std::snprintf(response, response_capacity, "Could not prepare the selected voice: %s", audiocpp_last_error());
        return 7;
    }
    if (progress) progress("Generating speech with Chatterbox");
    audiocpp_result *raw_result = nullptr;
    if (audiocpp_session_run(g_session.get(), raw_request, &raw_result) != AUDIOCPP_OK || !raw_result) {
        log_failure("Vulkan generation failed");
        std::snprintf(response, response_capacity, "Chatterbox speech generation failed: %s", audiocpp_last_error());
        release_engine();
        return 8;
    }
    const auto result = std::unique_ptr<audiocpp_result, decltype(&audiocpp_result_free)>(raw_result,
                                                                                        audiocpp_result_free);
    const float *samples = nullptr;
    std::size_t frames = 0;
    int sample_rate = 0, channels = 0;
    if (audiocpp_result_audio(raw_result, &samples, &frames, &sample_rate, &channels) != AUDIOCPP_OK ||
        !samples || !frames) {
        std::snprintf(response, response_capacity, "Chatterbox returned no speech audio.");
        return 9;
    }
    if (sample_rate <= 0 || frames < static_cast<std::size_t>(sample_rate / 4)) {
        log_line("invalid speech: decoder returned less than 250 ms of audio");
        std::snprintf(response,response_capacity,"Chatterbox stopped before producing speech. Please try again.");
        return 9;
    }
    char output[160];
    if (!prospero_generated_output_path(output, sizeof(output), "voice", "chatterbox-speech", "wav") ||
        !write_wav(output, samples, frames, sample_rate, channels)) {
        std::snprintf(response, response_capacity, "Could not save Chatterbox speech output.");
        return 10;
    }
    if (elapsed_microseconds) *elapsed_microseconds = sceKernelGetProcessTime() - start;
    char line[160];
    std::snprintf(line, sizeof(line), "generated %zu frames at %d Hz, %d channel(s)",
                  frames, sample_rate, channels);
    log_line(line);
    std::snprintf(response, response_capacity, "Speech generated locally: %s", output);
    return 0;
}
