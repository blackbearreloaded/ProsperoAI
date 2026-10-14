// SPDX-License-Identifier: GPL-3.0-or-later
#include "voice_recorder.hpp"
#include "runtime_voice_catalog.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#if !defined(PROSPERO_HOST)
extern "C"
{
    int sceUserServiceGetInitialUser(int *);
    int sceAudioInInit();
    int sceAudioInOpen(int user, unsigned type, unsigned index, unsigned frames, unsigned rate,
                       unsigned format);
    int sceAudioInInput(int handle, void *samples);
    int sceAudioInClose(int handle);
}
#endif

namespace prospero
{
VoiceRecorder &voice_recorder()
{
    static VoiceRecorder recorder;
    return recorder;
}
VoiceRecorder::~VoiceRecorder()
{
    discard();
}
void VoiceRecorder::join()
{
    if (joinable_)
    {
        pthread_join(thread_, nullptr);
        joinable_ = false;
    }
}
void VoiceRecorder::stop()
{
    stop_.store(true, std::memory_order_relaxed);
}
void VoiceRecorder::discard()
{
    std::lock_guard<std::mutex> lock(mutex_);
    discard_locked();
}
void VoiceRecorder::discard_locked()
{
    stop();
    join();
    samples_.fill(0);
    frames_ = 0;
    level_ = 0;
    state_.store(State::Idle, std::memory_order_release);
    std::remove(preview_path);
}
bool VoiceRecorder::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state() == State::Recording)
        return false;
    discard_locked();
    error_.clear();
    stop_ = false;
    state_.store(State::Recording, std::memory_order_release);
    if (pthread_create(&thread_, nullptr, worker, this) != 0)
    {
        error_ = "Could not start microphone recording.";
        state_.store(State::Failed, std::memory_order_release);
        return false;
    }
    joinable_ = true;
    return true;
}
void *VoiceRecorder::worker(void *self)
{
    static_cast<VoiceRecorder *>(self)->capture();
    return nullptr;
}
void VoiceRecorder::capture()
{
#if defined(PROSPERO_HOST)
    error_ = "Microphone recording requires a PS5.";
    state_.store(State::Failed, std::memory_order_release);
#else
    int user = -1;
    int rc = sceUserServiceGetInitialUser(&user);
    int handle = -1;
    if (rc == 0)
    {
        // AudioIn may already be initialized by another component.
        sceAudioInInit();
        handle = sceAudioInOpen(user, 1, 0, 256, 16000, 0);
        if (handle < 0)
            rc = handle;
    }
    unsigned count = 0, peak = 0;
    if (handle >= 0)
    {
        alignas(64) std::int16_t block[256];
        while (!stop_.load(std::memory_order_relaxed) && count < samples_.size())
        {
            rc = sceAudioInInput(handle, block);
            if (rc < 0)
                break;
            unsigned block_peak = 0;
            const unsigned copied = std::min<unsigned>(256, samples_.size() - count);
            for (unsigned i = 0; i < copied; ++i)
            {
                samples_[count + i] = block[i];
                block_peak = std::max(block_peak,
                                      static_cast<unsigned>(std::abs(static_cast<int>(block[i]))));
            }
            count += copied;
            peak = std::max(peak, block_peak);
            frames_.store(count, std::memory_order_relaxed);
            level_.store(block_peak, std::memory_order_relaxed);
        }
        sceAudioInClose(handle);
    }
    if (rc < 0 || handle < 0)
    {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "Microphone unavailable (0x%08x). Check PS5 input settings.",
                      static_cast<unsigned>(rc));
        error_ = message;
    }
    else if (count < 16000)
        error_ = "Record at least one second of speech.";
    else if (peak < 32)
        error_ = "No voice detected. Unmute your microphone and try again.";
    else
    {
        state_.store(State::Ready, std::memory_order_release);
        return;
    }
    state_.store(State::Failed, std::memory_order_release);
#endif
}
bool VoiceRecorder::write(const char *path)
{
    if (state() != State::Ready)
        return false;
    join();
    const std::string temporary = std::string(path) + ".recording.part";
    std::FILE *f = std::fopen(temporary.c_str(), "wb");
    if (!f)
    {
        error_ = "Could not save your voice sample.";
        return false;
    }
    unsigned char header[44]{};
    auto put = [&](unsigned offset, unsigned value, unsigned bytes)
    {
        for (unsigned i = 0; i < bytes; ++i)
            header[offset + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    std::copy_n("RIFF", 4, header);
    std::copy_n("WAVEfmt ", 8, header + 8);
    std::copy_n("data", 4, header + 36);
    const unsigned bytes = frames_.load() * 2;
    put(4, 36 + bytes, 4);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, 1, 2);
    put(24, 16000, 4);
    put(28, 32000, 4);
    put(32, 2, 2);
    put(34, 16, 2);
    put(40, bytes, 4);
    bool ok =
        std::fwrite(header, 1, 44, f) == 44 && std::fwrite(samples_.data(), 1, bytes, f) == bytes;
    if (std::fclose(f) != 0)
        ok = false;
    if (ok)
        ok = std::rename(temporary.c_str(), path) == 0;
    if (!ok)
    {
        std::remove(temporary.c_str());
        error_ = "Could not save your voice sample.";
    }
    return ok;
}
bool VoiceRecorder::preview()
{
    std::lock_guard<std::mutex> lock(mutex_);
    mkdir("/download0/ProsperoAI/work", 0777);
    mkdir("/download0/ProsperoAI/work/voice", 0777);
    return write(preview_path);
}
bool VoiceRecorder::save()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto directory = prospero_voice::runtime_voice_directory(true);
    mkdir(directory.c_str(), 0777);
    return write((directory + "/custom.wav").c_str());
}
} // namespace prospero
