// ProsperoAI - Playing a generated sound.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A generated sound is played through the interface's mixer. The console gives a
// title one main audio port, and the interface holds it for its own sounds: a
// second port, which this file used to open, is refused, and nothing was heard.
#include "media_output.hpp"

#include "audio/mixer.hpp"

#include <SDL2/SDL.h>

#include <chrono>
#include <limits>
#include <mutex>

namespace
{
using Clock = std::chrono::steady_clock;

std::mutex mutex;
hui::audio::Mixer *mixer;
float *samples; // interleaved stereo at the mixer's rate; the mixer reads it while it plays
hui::audio::Clip clip;
Clock::time_point ends;
bool started;

// The sound as the mixer wants it. Takes over `*data` (from SDL_LoadWAV).
bool convert(Uint8 **data, Uint32 *bytes, const SDL_AudioSpec &source)
{
    SDL_AudioCVT conversion{};
    const int required = SDL_BuildAudioCVT(&conversion, source.format, source.channels, source.freq,
                                           AUDIO_F32SYS, 2, hui::audio::kSampleRate);
    if (required < 0)
        return false;
    if (required == 0)
        return true;
    if (*bytes > static_cast<Uint32>(std::numeric_limits<int>::max()) || conversion.len_mult <= 0 ||
        *bytes > std::numeric_limits<Uint32>::max() / static_cast<Uint32>(conversion.len_mult))
        return false;
    conversion.len = static_cast<int>(*bytes);
    conversion.buf =
        static_cast<Uint8 *>(SDL_malloc(*bytes * static_cast<Uint32>(conversion.len_mult)));
    if (!conversion.buf)
        return false;
    SDL_memcpy(conversion.buf, *data, *bytes);
    if (SDL_ConvertAudio(&conversion) != 0)
    {
        SDL_free(conversion.buf);
        return false;
    }
    SDL_FreeWAV(*data);
    *data = conversion.buf;
    *bytes = static_cast<Uint32>(conversion.len_cvt);
    return true;
}

void stop_locked()
{
    if (!samples)
        return;
    if (started && mixer && Clock::now() < ends)
    {
        // The mixer reads the samples from its own thread: it is told to stop, and
        // two of its blocks pass before they are freed.
        mixer->stop_all();
        SDL_Delay(40);
    }
    SDL_free(samples);
    samples = nullptr;
    clip = {};
    started = false;
}
} // namespace

void prospero_media_attach(hui::audio::Mixer *interface_mixer)
{
    const std::lock_guard<std::mutex> lock(mutex);
    stop_locked();
    mixer = interface_mixer;
}

extern "C" void ps5_media_stop()
{
    const std::lock_guard<std::mutex> lock(mutex);
    stop_locked();
}

extern "C" void ps5_media_shutdown()
{
    ps5_media_stop();
}

extern "C" int ps5_media_is_playing()
{
    const std::lock_guard<std::mutex> lock(mutex);
    return started && Clock::now() < ends ? 1 : 0;
}

extern "C" int ps5_media_play_wav(const char *path)
{
    const std::lock_guard<std::mutex> lock(mutex);
    stop_locked();
    if (!path)
        return 1;
    if (!mixer)
        return 4;

    SDL_AudioSpec source{};
    Uint8 *data = nullptr;
    Uint32 bytes = 0;
    if (!SDL_LoadWAV(path, &source, &data, &bytes))
        return 2;
    if (!convert(&data, &bytes, source))
    {
        SDL_FreeWAV(data);
        return 3;
    }
    const std::size_t frames = bytes / (2 * sizeof(float));
    if (frames == 0)
    {
        SDL_free(data);
        return 3;
    }
    samples = reinterpret_cast<float *>(data);
    clip = {samples, frames};
    hui::audio::PlayParams how;
    how.bus = hui::audio::Bus::music; // not the interface sounds' volume setting
    if (!mixer->play_clip(&clip, how))
    {
        SDL_free(samples);
        samples = nullptr;
        clip = {};
        return 6;
    }
    started = true;
    ends = Clock::now() + std::chrono::milliseconds(frames * 1000 / hui::audio::kSampleRate + 150);
    return 0;
}
