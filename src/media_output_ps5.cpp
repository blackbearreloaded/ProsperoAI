#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

extern "C"
{
    int sceAudioOutInit(void);
    int sceAudioOutOpen(int, int, int, std::uint32_t, std::uint32_t, std::uint32_t);
    int sceAudioOutClose(int);
    int sceAudioOutOutput(int, const void *);
    int sceAudioOutSetVolume(int, int, const int *);
}

namespace
{

constexpr Uint32 audio_rate = 48000;
constexpr Uint32 audio_grain = 256;
constexpr Uint32 audio_format = 1; // signed 16-bit stereo, interleaved
constexpr Uint32 audio_block_bytes = audio_grain * 2 * sizeof(std::int16_t);

SDL_Thread *audio_thread;
Uint8 *audio_samples;
Uint32 audio_sample_bytes;
std::atomic<bool> stop_requested{};
std::atomic<bool> playing{};
int audio_handle = -1;

bool convert_audio(Uint8 **samples, Uint32 *bytes, const SDL_AudioSpec &source)
{
    SDL_AudioCVT conversion{};
    const int required = SDL_BuildAudioCVT(&conversion, source.format, source.channels, source.freq,
                                           AUDIO_S16SYS, 2, audio_rate);
    if (required < 0)
        return false;
    if (!required)
        return true;
    if (*bytes > static_cast<Uint32>(std::numeric_limits<int>::max()) || conversion.len_mult <= 0 ||
        *bytes > std::numeric_limits<Uint32>::max() / static_cast<Uint32>(conversion.len_mult))
        return false;
    conversion.len = static_cast<int>(*bytes);
    conversion.buf =
        static_cast<Uint8 *>(SDL_malloc(*bytes * static_cast<Uint32>(conversion.len_mult)));
    if (!conversion.buf)
        return false;
    SDL_memcpy(conversion.buf, *samples, *bytes);
    if (SDL_ConvertAudio(&conversion) != 0)
    {
        SDL_free(conversion.buf);
        return false;
    }
    SDL_FreeWAV(*samples);
    *samples = conversion.buf;
    *bytes = static_cast<Uint32>(conversion.len_cvt);
    return true;
}

int play_audio(void *)
{
    alignas(64) std::array<Uint8, audio_block_bytes> block{};
    Uint32 offset = 0;
    while (offset < audio_sample_bytes && !stop_requested.load(std::memory_order_acquire))
    {
        const Uint32 bytes = std::min(audio_block_bytes, audio_sample_bytes - offset);
        SDL_memset(block.data(), 0, block.size());
        SDL_memcpy(block.data(), audio_samples + offset, bytes);
        if (sceAudioOutOutput(audio_handle, block.data()) < 0)
            break;
        offset += bytes;
    }
    sceAudioOutOutput(audio_handle, nullptr);
    sceAudioOutClose(audio_handle);
    audio_handle = -1;
    playing.store(false, std::memory_order_release);
    return 0;
}

} // namespace

extern "C" void ps5_media_stop()
{
    if (audio_thread)
    {
        stop_requested.store(true, std::memory_order_release);
        SDL_WaitThread(audio_thread, nullptr);
        audio_thread = nullptr;
    }
    SDL_FreeWAV(audio_samples);
    audio_samples = nullptr;
    audio_sample_bytes = 0;
    stop_requested.store(false, std::memory_order_release);
    playing.store(false, std::memory_order_release);
}

extern "C" void ps5_media_shutdown()
{
    ps5_media_stop();
}

extern "C" int ps5_media_is_playing()
{
    return playing.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" int ps5_media_play_wav(const char *path)
{
    ps5_media_stop();
    if (!path)
        return 1;

    SDL_AudioSpec source{};
    Uint8 *samples = nullptr;
    Uint32 sample_bytes = 0;
    if (!SDL_LoadWAV(path, &source, &samples, &sample_bytes))
        return 2;
    if (!convert_audio(&samples, &sample_bytes, source))
    {
        SDL_FreeWAV(samples);
        return 3;
    }

    if (sceAudioOutInit() < 0 ||
        (audio_handle = sceAudioOutOpen(0xff, 0, 0, audio_grain, audio_rate, audio_format)) < 0)
    {
        SDL_FreeWAV(samples);
        return 4;
    }
    std::array<int, 8> volumes{};
    volumes.fill(0x8000);
    if (sceAudioOutSetVolume(audio_handle, 3, volumes.data()) < 0)
    {
        sceAudioOutClose(audio_handle);
        audio_handle = -1;
        SDL_FreeWAV(samples);
        return 5;
    }

    audio_samples = samples;
    audio_sample_bytes = sample_bytes;
    playing.store(true, std::memory_order_release);
    audio_thread = SDL_CreateThread(play_audio, "ProsperoAudio", nullptr);
    if (!audio_thread)
    {
        playing.store(false, std::memory_order_release);
        sceAudioOutClose(audio_handle);
        audio_handle = -1;
        SDL_FreeWAV(audio_samples);
        audio_samples = nullptr;
        audio_sample_bytes = 0;
        return 6;
    }
    return 0;
}
