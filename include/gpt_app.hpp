// ProsperoAI - Native PlayStation 5 local AI chat application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gpt_input.hpp"
#include "gpt_runtime.hpp"
#include "session_store.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Rml {
class ElementDocument;
}

class ProsperoGptApp {
public:
    bool Initialize(Rml::ElementDocument* document);
    void Poll();
    void HandleInput(const gpt_input_event_t& event);
    void Shutdown();

private:
    static constexpr unsigned MessageCapacity = prospero_session::MessageCapacity;
    static constexpr unsigned SessionCapacity = prospero_session::CatalogCapacity;
    static constexpr unsigned VisibleSessionCount = 7;
    using Message = prospero_session::Message;

    enum class View { Conversation, Workshop };

    Rml::ElementDocument* document_ = nullptr;
    Message history_[MessageCapacity]{};
    unsigned history_count_ = 0;
    prospero_session::Record current_session_{};
    prospero_session::Record sessions_[SessionCapacity]{};
    unsigned session_count_ = 0;
    unsigned session_selection_ = 0;
    unsigned session_window_ = 0;
    bool session_rail_focused_ = false;
    bool session_model_available_ = true;
    unsigned output_limit_ = 64;
    unsigned style_ = 0;
    unsigned settings_focus_ = 0;
    View view_ = View::Conversation;
    void* generation_thread_ = nullptr;
    int generation_result_ = 0;
    bool generating_ = false;
    bool pending_ = false;
    char pending_text_[1024]{};
    char status_text_[96] = "Ready";
    char generation_timestamp_[9]{};
    char generation_response_[4096]{};
    gpt_runtime_stats_t generation_stats_{};
    std::atomic<bool> generation_done_{false};
    std::atomic<unsigned> stream_version_{0};
    std::atomic_flag stream_lock_ = ATOMIC_FLAG_INIT;
    unsigned displayed_stream_version_ = 0;
    char stream_text_[4096]{};

    void AddMessage(const char* role, const char* content);
    void BeginNewSession();
    bool EnsureSession();
    void SaveSession();
    void RefreshSessions();
    void RefreshAudioAction();
    void OpenSelectedSession();
    void DeleteSelectedSession();
    bool FindLatestAudio(char* path, std::size_t capacity) const;
    void PlayLatestAudio();
    int FindModel(const char* id) const;
    void ArchiveGeneratedMedia();
    void BuildMessageRows();
    void SetStatus(const char* text);
    void StartWarmup(const char* status);
    void SelectModel(unsigned index);
    void OpenKeyboard();
    bool StartGeneration();
    void FinishGeneration();
    void PublishStream(const char* text);
    void CopyStream(char* text, std::size_t capacity);
    void RefreshAll();
    void RefreshConversation();
    void RefreshSettings();
    void RefreshStatus();
    void SetView(View view);
    void ChangeSetting(int direction);
    void LoadSettings();
    void SaveSettings() const;

    static void* GenerationWorker(void* user_data);
    static void StreamCallback(const char* text);
    static void ImeResult(const char* text, void* user_data);
    static ProsperoGptApp* active_;
};
