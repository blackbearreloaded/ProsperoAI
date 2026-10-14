// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <mutex>
#include <string>

namespace prospero {
class VoiceRecorder {
public:
    enum class State { Idle, Recording, Ready, Failed };
    ~VoiceRecorder();
    bool start();
    void stop();
    void discard();
    bool save();
    bool preview();
    State state() const { return state_.load(std::memory_order_acquire); }
    float seconds() const { return frames_.load(std::memory_order_relaxed) / 16000.0f; }
    unsigned level() const { return level_.load(std::memory_order_relaxed); }
    std::string error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state() == State::Recording ? std::string{} : error_;
    }
    static constexpr const char *preview_path = "/download0/ProsperoAI/work/voice/microphone-preview.wav";
private:
    static void *worker(void *self);
    void capture();
    void join();
    void discard_locked();
    bool write(const char *path);
    std::array<std::int16_t, 80000> samples_{};
    std::atomic<State> state_{State::Idle};
    std::atomic<unsigned> frames_{0}, level_{0};
    std::atomic<bool> stop_{false};
    pthread_t thread_{};
    bool joinable_ = false;
    std::string error_;
    mutable std::mutex mutex_;
};
VoiceRecorder &voice_recorder();
}
