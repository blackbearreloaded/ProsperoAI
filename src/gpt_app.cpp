// ProsperoAI - Native PlayStation 5 local AI chat application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpt_app.hpp"

#include "gpt_ime.hpp"
#include "gpt_runtime.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <string>
#include <sys/time.h>

extern "C"
{
    int sceKernelDebugOutText(int channel, const char *text);
    int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                         void *argument, const char *name);
    int scePthreadJoin(void *thread, void **result);
}

int gpt_runtime_prepare();
bool gpt_runtime_context_full();
#if defined(PS5_MEDIA_AUDIO)
extern "C" int ps5_media_play_wav(const char *path);
extern "C" int ps5_media_is_playing(void);
extern "C" void ps5_media_shutdown(void);
#endif

namespace
{

constexpr const char *kSettingsPath = "/download0/prosperoai.cfg";
constexpr const char *kComposerPrompt = "Ask ProsperoAI anything...";
constexpr const char *kStyleNames[] = {"Balanced", "Precise", "Creative"};
constexpr const char *kStyleDescriptions[] = {
    "Clear, thoughtful answers for everyday questions.",
    "Direct answers with concise language and stated uncertainty.",
    "Imaginative exploration that stays grounded and useful.",
};
constexpr const char *kSystemPrompts[] = {
    "You are ProsperoAI, a thoughtful AI assistant running locally on a PlayStation 5. "
    "Be clear, helpful, and concise.",
    "You are ProsperoAI, a precise AI assistant running locally on a PlayStation 5. "
    "Answer directly with concise factual language and state uncertainty.",
    "You are ProsperoAI, a creative AI assistant running locally on a PlayStation 5. "
    "Explore imaginative options while staying grounded, helpful, and concise.",
};
constexpr unsigned kOutputLimits[] = {64, 128, 192, 256};
constexpr int kMessageGap = 6;
constexpr int kMessageMinimumHeight = 86;
constexpr int kConversationViewportHeight = 588;
constexpr float kConversationScrollStep = 96.0f;
bool runtime_warm;
bool warming;
bool conversation_auto_follow = true;
int conversation_scroll_offset;
int conversation_content_height;
bool automatic_chat_test;
std::FILE *automatic_chat_file;
unsigned automatic_scroll_frame;
unsigned busy_animation_frame = 4;
#if defined(PS5_MEDIA_AUDIO)
unsigned audio_animation_frame = 4;
char playing_audio_path[320];
#endif
#if defined(PS5_MEDIA_SWITCH_AUTOTEST)
bool automatic_media_switch_started;
#endif
void *warmup_thread;
int warmup_result;
std::atomic<bool> warmup_done;

const char *PurposeLabel(const char *purpose)
{
    if (purpose && std::strcmp(purpose, "text-to-image") == 0)
        return "Text to image";
    if (purpose && std::strcmp(purpose, "text-to-audio") == 0)
        return "Text to audio";
    if (purpose && std::strcmp(purpose, "text-to-speech") == 0)
        return "Text to speech";
    return "Text to text";
}

bool IsAudioPurpose(const char *purpose)
{
    return purpose && (std::strcmp(purpose, "text-to-audio") == 0 ||
                       std::strcmp(purpose, "text-to-speech") == 0);
}

bool IsMediaPurpose(const char *purpose)
{
    return purpose && (std::strcmp(purpose, "text-to-image") == 0 || IsAudioPurpose(purpose));
}

void *WarmupWorker(void *)
{
    warmup_result = gpt_runtime_prepare();
    warmup_done.store(true, std::memory_order_release);
    return nullptr;
}

Rml::Element *Find(Rml::ElementDocument *document, const char *id)
{
    return document ? document->GetElementById(id) : nullptr;
}

void SetText(Rml::ElementDocument *document, const char *id, const char *text)
{
    Rml::Element *element = Find(document, id);
    if (!element)
        return;
    Rml::String encoded = Rml::StringUtilities::EncodeRml(text ? text : "");
    for (std::size_t at = 0; (at = encoded.find('\n', at)) != Rml::String::npos;)
    {
        encoded.replace(at, 1, "<br/>");
        at += 5;
    }
    if (element->GetInnerRML() != encoded)
        element->SetInnerRML(encoded);
}

void SetClass(Rml::ElementDocument *document, const char *id, const char *class_name, bool enabled)
{
    if (Rml::Element *element = Find(document, id))
        element->SetClass(class_name, enabled);
}

void SetVisible(Rml::ElementDocument *document, const char *id, bool visible)
{
    SetClass(document, id, "hidden", !visible);
}

void SetPixelProperty(Rml::ElementDocument *document, const char *id, const char *name, int value)
{
    if (Rml::Element *element = Find(document, id))
    {
        char text[24];
        std::snprintf(text, sizeof(text), "%dpx", value);
        element->SetProperty(name, text);
    }
}

unsigned SetMessageText(Rml::ElementDocument *document, const char *id, const char *text)
{
    Rml::Element *element = Find(document, id);
    if (!element)
        return 1;

    constexpr std::size_t kColumns = 76;
    constexpr int kLineHeight = 26;
    const char *image_path = text ? std::strstr(text, "/download0/") : nullptr;
    const char *image_end = image_path ? std::strstr(image_path, ".tga") : nullptr;
    if (!image_end)
        image_path = nullptr;
    const char *audio_path = text ? std::strstr(text, "/download0/") : nullptr;
    const char *audio_end = audio_path ? std::strstr(audio_path, ".wav") : nullptr;
    if (!audio_end)
        audio_path = nullptr;
    const char *media_path = image_path ? image_path : audio_path;
    Rml::String markup;
    std::string line;
    unsigned line_count = 0;
    const auto emit_line = [&]()
    {
        char opening[96];
        std::snprintf(opening, sizeof(opening),
                      "<span class=\"message-line\" style=\"top: %dpx;\">",
                      static_cast<int>(line_count) * kLineHeight);
        markup += opening;
        markup += Rml::StringUtilities::EncodeRml(line);
        markup += "</span>";
        line.clear();
        ++line_count;
    };

    const char *cursor = text ? text : "";
    while (*cursor && cursor != media_path)
    {
        if (*cursor == '\n')
        {
            emit_line();
            ++cursor;
            continue;
        }
        if (*cursor == ' ' || *cursor == '\t' || *cursor == '\r')
        {
            ++cursor;
            continue;
        }

        const char *word_begin = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
            ++cursor;
        const std::size_t word_length = static_cast<std::size_t>(cursor - word_begin);
        if (!line.empty() && line.size() + 1 + word_length > kColumns)
            emit_line();
        if (!line.empty())
            line += ' ';
        line.append(word_begin, word_length);
    }
    if (!line.empty() || line_count == 0)
        emit_line();

    if (image_path && image_end && image_end - image_path < 240)
    {
        Rml::String path(image_path, static_cast<std::size_t>(image_end + 4 - image_path));
        markup += "<img class=\"generated-image\" src=\"";
        markup += Rml::StringUtilities::EncodeRml(path);
        markup += "\"/>";
        line_count += 16;
    }
    else if (audio_path && audio_end && audio_end - audio_path < 240)
    {
#if defined(PS5_MEDIA_AUDIO)
        const std::size_t path_length = static_cast<std::size_t>(audio_end + 4 - audio_path);
        const bool playing = ps5_media_is_playing() &&
                             std::strlen(playing_audio_path) == path_length &&
                             std::memcmp(playing_audio_path, audio_path, path_length) == 0;
        constexpr const char *waves[] = {"|  ||  |||", "||  |||  |", "|||  |  ||", "||  |  |||"};
#else
        const bool playing = false;
#endif
        char control[512];
        std::snprintf(control, sizeof(control),
                      "<span class=\"audio-control%s\" style=\"top: %dpx;\">"
                      "<span class=\"audio-play-icon\">&gt;</span>"
                      "<span class=\"audio-control-label\">%s</span>"
                      "<span class=\"audio-control-hint\">TRIANGLE</span>"
                      "<span class=\"audio-wave\">%s</span></span>",
                      playing ? " playing" : "", static_cast<int>(line_count) * kLineHeight,
                      playing ? "PLAYING" : "PLAY AUDIO",
#if defined(PS5_MEDIA_AUDIO)
                      playing ? waves[audio_animation_frame % 4] : ""
#else
                      ""
#endif
        );
        markup += control;
        line_count += 2;
    }

    if (element->GetInnerRML() != markup)
        element->SetInnerRML(markup);
    char height[24];
    std::snprintf(height, sizeof(height), "%dpx", static_cast<int>(line_count) * kLineHeight);
    element->SetProperty("height", height);
    return line_count;
}

void CurrentTimestamp(char *output, std::size_t capacity)
{
    timeval now{};
    struct timezone zone
    {
    };
    if (gettimeofday(&now, &zone) != 0)
    {
        std::snprintf(output, capacity, "--:--:--");
        return;
    }
    long long seconds =
        static_cast<long long>(now.tv_sec) - static_cast<long long>(zone.tz_minuteswest) * 60LL;
    seconds %= 24LL * 60LL * 60LL;
    if (seconds < 0)
        seconds += 24LL * 60LL * 60LL;
    std::snprintf(output, capacity, "%02lld:%02lld:%02lld", seconds / 3600LL,
                  (seconds / 60LL) % 60LL, seconds % 60LL);
}

unsigned BusyAnimationFrame()
{
    timeval now{};
    if (gettimeofday(&now, nullptr) != 0)
        return 0;
    const unsigned long long milliseconds = static_cast<unsigned long long>(now.tv_sec) * 1000ULL +
                                            static_cast<unsigned long long>(now.tv_usec) / 1000ULL;
    return static_cast<unsigned>((milliseconds / 350ULL) % 4ULL);
}

int ConversationScrollMaximum()
{
    return conversation_content_height > kConversationViewportHeight
               ? conversation_content_height - kConversationViewportHeight
               : 0;
}

bool ScrollConversation(float delta)
{
    const int maximum = ConversationScrollMaximum();
    int target = conversation_scroll_offset + static_cast<int>(delta);
    if (target < 0)
        target = 0;
    if (target > maximum)
        target = maximum;
    const bool moved = target != conversation_scroll_offset;
    conversation_scroll_offset = target;
    conversation_auto_follow = target >= maximum;
    char log[112];
    std::snprintf(log, sizeof(log), "[prosperoai] chat_scroll delta=%.0f top=%d max=%d follow=%u\n",
                  delta, target, maximum, conversation_auto_follow ? 1U : 0U);
    sceKernelDebugOutText(0, log);
    return moved;
}

unsigned OutputLimitIndex(unsigned value)
{
    for (unsigned i = 0; i < sizeof(kOutputLimits) / sizeof(kOutputLimits[0]); ++i)
        if (kOutputLimits[i] == value)
            return i;
    return 1;
}

bool IsOutputLimit(unsigned value)
{
    for (unsigned limit : kOutputLimits)
        if (limit == value)
            return true;
    return false;
}

} // namespace

ProsperoAiApp *ProsperoAiApp::active_ = nullptr;

bool ProsperoAiApp::Initialize(Rml::ElementDocument *document)
{
    document_ = document;
    if (!document_)
        return false;
    active_ = this;
    conversation_auto_follow = true;
    conversation_scroll_offset = 0;
    conversation_content_height = 0;
    automatic_chat_test = false;
    automatic_scroll_frame = 0;
#if defined(PS5_MEDIA_SWITCH_AUTOTEST)
    automatic_media_switch_started = false;
#endif
    LoadSettings();
    BuildMessageRows();
    RefreshSessions();
#if defined(PS5_TEXT_REGRESSION_AUTOTEST)
    output_limit_ = 32;
    for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
    {
        if (std::strcmp(gpt_runtime_model_purpose(index), "text-to-text") == 0)
        {
            gpt_runtime_select_model(index);
            break;
        }
    }
    std::snprintf(pending_text_, sizeof(pending_text_), "Hello");
    pending_ = true;
    automatic_chat_test = true;
    SetStatus("Automatic text GPU regression queued");
#elif defined(PS5_MEDIA_IMAGE_AUTOTEST)
    for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
    {
        if (std::strcmp(gpt_runtime_model_purpose(index), "text-to-image") == 0)
        {
            gpt_runtime_select_model(index);
            break;
        }
    }
    std::snprintf(pending_text_, sizeof(pending_text_),
                  "A small red robot standing beside a blue cube, studio lighting");
    pending_ = true;
    automatic_chat_test = true;
    SetStatus("Automatic image GPU test queued");
#elif defined(PS5_MEDIA_AUDIO_AUTOTEST)
    for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
    {
        if (std::strcmp(gpt_runtime_model_purpose(index), "text-to-audio") == 0)
        {
            gpt_runtime_select_model(index);
            break;
        }
    }
    std::snprintf(pending_text_, sizeof(pending_text_), "128 BPM tech house drum loop");
    pending_ = true;
    automatic_chat_test = true;
    SetStatus("Automatic audio GPU test queued");
#elif defined(PS5_MEDIA_TTS_AUTOTEST)
    for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
    {
        if (std::strcmp(gpt_runtime_model_purpose(index), "text-to-speech") == 0)
        {
            gpt_runtime_select_model(index);
            break;
        }
    }
    std::snprintf(pending_text_, sizeof(pending_text_), "Whatever you want it to say.");
    pending_ = true;
    automatic_chat_test = true;
    SetStatus("Automatic speech GPU test queued");
#else
    automatic_chat_file = std::fopen("/app0/auto_chat.txt", "rb");
    if (automatic_chat_file)
    {
        if (std::fgets(pending_text_, sizeof(pending_text_), automatic_chat_file))
        {
            pending_text_[std::strcspn(pending_text_, "\r\n")] = '\0';
            pending_ = pending_text_[0] != '\0';
            if (pending_)
            {
                automatic_chat_test = true;
                SetStatus("Automatic GPU test queued");
            }
        }
    }
#endif
    RefreshAll();
    sceKernelDebugOutText(0, "[prosperoai] ui_ready=1\n");
    StartWarmup("Loading the selected model in the background");
    return true;
}

void ProsperoAiApp::Shutdown()
{
    if (generating_)
    {
        scePthreadJoin(generation_thread_, nullptr);
        generating_ = false;
    }
    if (warming)
    {
        scePthreadJoin(warmup_thread, nullptr);
        warming = false;
    }
    if (automatic_chat_file)
    {
        std::fclose(automatic_chat_file);
        automatic_chat_file = nullptr;
    }
#if defined(PS5_MEDIA_AUDIO)
    ps5_media_shutdown();
#endif
    active_ = nullptr;
    document_ = nullptr;
}

void ProsperoAiApp::SetStatus(const char *text)
{
    std::snprintf(status_text_, sizeof(status_text_), "%s", text ? text : "");
    RefreshStatus();
}

void ProsperoAiApp::StartWarmup(const char *status)
{
    if (!gpt_runtime_available())
        return;
    warmup_done.store(false, std::memory_order_relaxed);
    warming = true;
    if (scePthreadCreate(&warmup_thread, nullptr, WarmupWorker, nullptr, "prosperoai-gpu-warmup") !=
        0)
    {
        warming = false;
        SetStatus("Could not start background model load");
    }
    else
    {
        SetStatus(status);
    }
    RefreshSettings();
}

void ProsperoAiApp::SelectModel(unsigned index)
{
    if (generating_ || warming || index == gpt_runtime_selected_model() ||
        !gpt_runtime_select_model(index))
        return;
    BeginNewSession();
    runtime_warm = false;
    SaveSettings();
    RefreshAll();
    StartWarmup("Switching GPU model");
}

void ProsperoAiApp::AddMessage(const char *role, const char *content)
{
    if (history_count_ == MessageCapacity)
    {
        SetStatus("This session is full  ·  press Square to start another");
        return;
    }
    if (!EnsureSession())
    {
        SetStatus("Could not create a session on disk");
        return;
    }
    std::snprintf(history_[history_count_].role, sizeof(history_[history_count_].role), "%s", role);
    CurrentTimestamp(history_[history_count_].timestamp,
                     sizeof(history_[history_count_].timestamp));
    std::snprintf(history_[history_count_].content, sizeof(history_[history_count_].content), "%s",
                  content);
    ++history_count_;
    if (history_count_ == 1 && std::strcmp(role, "user") == 0)
    {
        unsigned length = 0;
        while (content[length] && content[length] != '\n' && length < 54)
        {
            const unsigned char value = static_cast<unsigned char>(content[length]);
            current_session_.title[length] = value >= 32 ? content[length] : ' ';
            ++length;
        }
        current_session_.title[length] = '\0';
        if (content[length])
            std::snprintf(current_session_.title + length, sizeof(current_session_.title) - length,
                          "...");
    }
    SaveSession();
}

void ProsperoAiApp::BeginNewSession()
{
    if (generating_)
        return;
    current_session_ = {};
    history_count_ = 0;
    pending_ = false;
    pending_text_[0] = '\0';
    stream_text_[0] = '\0';
    conversation_auto_follow = true;
    conversation_scroll_offset = 0;
    session_model_available_ = gpt_runtime_available();
    session_rail_focused_ = false;
    session_selection_ = 0;
    session_window_ = 0;
    SetStatus("Ready for a new private session");
    RefreshSessions();
    RefreshConversation();
}

bool ProsperoAiApp::EnsureSession()
{
    if (current_session_.id[0])
        return true;
    if (!gpt_runtime_available())
        return false;
    const unsigned model = gpt_runtime_selected_model();
    session_model_available_ =
        prospero_session::create(&current_session_, gpt_runtime_model_id(model),
                                 gpt_runtime_model_name(model), gpt_runtime_model_purpose(model));
    return session_model_available_;
}

void ProsperoAiApp::SaveSession()
{
    if (!current_session_.id[0])
        return;
    if (!prospero_session::save(&current_session_, history_, history_count_))
    {
        SetStatus("Could not save the current session");
        return;
    }
    char log[192];
    std::snprintf(log, sizeof(log), "[prosperoai] session_saved id=%s messages=%u purpose=%s\n",
                  current_session_.id, history_count_, current_session_.purpose);
    sceKernelDebugOutText(0, log);
    RefreshSessions();
}

int ProsperoAiApp::FindModel(const char *id) const
{
    for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
        if (std::strcmp(gpt_runtime_model_id(index), id) == 0)
            return static_cast<int>(index);
    return -1;
}

void ProsperoAiApp::RefreshSessions()
{
    session_count_ = prospero_session::scan(sessions_, SessionCapacity);
    const unsigned maximum = session_count_;
    if (session_selection_ > maximum)
        session_selection_ = maximum;
    if (session_selection_ > 0 && session_selection_ <= session_window_)
        session_window_ = session_selection_ - 1;
    if (session_selection_ > session_window_ + VisibleSessionCount)
        session_window_ = session_selection_ - VisibleSessionCount;
    const unsigned largest_window =
        maximum > VisibleSessionCount ? maximum - VisibleSessionCount : 0;
    if (session_window_ > largest_window)
        session_window_ = largest_window;

    SetClass(document_, "session-new", "selected",
             session_rail_focused_ && session_selection_ == 0);
    for (unsigned row = 0; row < VisibleSessionCount; ++row)
    {
        const unsigned selection = session_window_ + row + 1;
        char row_id[32];
        char title_id[36];
        char meta_id[36];
        std::snprintf(row_id, sizeof(row_id), "session-row-%u", row);
        std::snprintf(title_id, sizeof(title_id), "session-title-%u", row);
        std::snprintf(meta_id, sizeof(meta_id), "session-meta-%u", row);
        const bool visible = selection > 0 && selection <= session_count_;
        SetVisible(document_, row_id, visible);
        if (!visible)
            continue;
        const prospero_session::Record &session = sessions_[selection - 1];
        SetText(document_, title_id, session.title);
        char metadata[96];
        std::snprintf(metadata, sizeof(metadata), "%s  ·  %s", session.updated_time,
                      PurposeLabel(session.purpose));
        SetText(document_, meta_id, metadata);
        SetClass(document_, row_id, "selected",
                 session_rail_focused_ && session_selection_ == selection);
        SetClass(document_, row_id, "active",
                 current_session_.id[0] && std::strcmp(current_session_.id, session.id) == 0);
    }
    char count[48];
    std::snprintf(count, sizeof(count), "%u saved session%s", session_count_,
                  session_count_ == 1 ? "" : "s");
    SetText(document_, "session-count", count);
    SetClass(document_, "composer", "focused", !session_rail_focused_);
    RefreshAudioAction();
}

void ProsperoAiApp::RefreshAudioAction()
{
    char path[320];
    const bool audio_available = FindLatestAudio(path, sizeof(path));
#if defined(PS5_MEDIA_AUDIO)
    const bool playing =
        audio_available && ps5_media_is_playing() && std::strcmp(path, playing_audio_path) == 0;
#else
    const bool playing = false;
#endif
    SetVisible(document_, "hint-triangle",
               view_ == View::Conversation && (session_rail_focused_ || audio_available));
    SetText(document_, "triangle-action",
            session_rail_focused_ ? "Delete"
            : playing             ? "Playing audio"
                                  : "Play audio");
}

void ProsperoAiApp::OpenSelectedSession()
{
    if (generating_ || warming)
        return;
    if (session_selection_ == 0)
    {
        BeginNewSession();
        return;
    }
    if (session_selection_ > session_count_)
        return;
    prospero_session::Record loaded{};
    const char *id = sessions_[session_selection_ - 1].id;
    if (!prospero_session::load(id, &loaded, history_, MessageCapacity))
    {
        SetStatus("Could not open the selected session");
        return;
    }
    current_session_ = loaded;
    history_count_ = loaded.message_count;
    pending_ = false;
    pending_text_[0] = '\0';
    stream_text_[0] = '\0';
    conversation_auto_follow = true;
    conversation_scroll_offset = 0;
    session_rail_focused_ = false;
    const int model = FindModel(loaded.model_id);
    session_model_available_ = model >= 0;
    if (model < 0)
    {
        SetStatus("Session model is not installed  ·  history is read-only");
    }
    else if (static_cast<unsigned>(model) != gpt_runtime_selected_model())
    {
        gpt_runtime_select_model(static_cast<unsigned>(model));
        runtime_warm = false;
        SaveSettings();
        StartWarmup("Restoring the session model");
    }
    else
    {
        SetStatus("Saved session restored");
    }
    char log[192];
    std::snprintf(log, sizeof(log),
                  "[prosperoai] session_opened id=%s messages=%u model=%s installed=%u\n",
                  current_session_.id, history_count_, current_session_.model_id,
                  session_model_available_ ? 1U : 0U);
    sceKernelDebugOutText(0, log);
    RefreshAll();
}

void ProsperoAiApp::DeleteSelectedSession()
{
    if (generating_ || warming || session_selection_ == 0 || session_selection_ > session_count_)
        return;
    const prospero_session::Record selected = sessions_[session_selection_ - 1];
    if (!prospero_session::erase(selected.id))
    {
        SetStatus("Could not delete the selected session");
        return;
    }
    const bool deleted_active =
        current_session_.id[0] && std::strcmp(current_session_.id, selected.id) == 0;
    if (deleted_active)
    {
        current_session_ = {};
        history_count_ = 0;
        conversation_scroll_offset = 0;
        conversation_auto_follow = true;
    }
    if (session_selection_ > 0)
        --session_selection_;
    char log[128];
    std::snprintf(log, sizeof(log), "[prosperoai] session_deleted id=%s\n", selected.id);
    sceKernelDebugOutText(0, log);
    SetStatus("Session and its media deleted");
    RefreshSessions();
    RefreshConversation();
}

bool ProsperoAiApp::FindLatestAudio(char *path, std::size_t capacity) const
{
    if (!path || capacity == 0)
        return false;
    path[0] = '\0';
    for (unsigned index = history_count_; index > 0; --index)
    {
        const char *begin = std::strstr(history_[index - 1].content, "/download0/");
        const char *end = begin ? std::strstr(begin, ".wav") : nullptr;
        if (!begin || !end)
            continue;
        const std::size_t length = static_cast<std::size_t>(end + 4 - begin);
        if (length >= capacity)
            continue;
        std::memcpy(path, begin, length);
        path[length] = '\0';
        return true;
    }
    return false;
}

void ProsperoAiApp::PlayLatestAudio()
{
#if defined(PS5_MEDIA_AUDIO)
    char path[320];
    if (!FindLatestAudio(path, sizeof(path)))
        return;
    const int result = ps5_media_play_wav(path);
    if (result == 0)
    {
        std::snprintf(playing_audio_path, sizeof(playing_audio_path), "%s", path);
        audio_animation_frame = BusyAnimationFrame();
    }
    SetStatus(result == 0 ? "Playing saved audio" : "Could not play saved audio");
    char log[448];
    std::snprintf(log, sizeof(log), "[prosperoai] audio_replay=%u rc=%08X path=%s\n",
                  result == 0 ? 1U : 0U, static_cast<unsigned>(result), path);
    sceKernelDebugOutText(0, log);
    RefreshConversation();
#endif
}

void ProsperoAiApp::OpenKeyboard()
{
    if (!gpt_runtime_available())
    {
        SetStatus("No models found  ·  add one to PPSA99004/models/");
        return;
    }
    if (!session_model_available_)
    {
        SetStatus("Install this session's model before continuing");
        return;
    }
    if (!generating_ && !pending_)
        gpt_ime_request(pending_text_, ImeResult, this);
}

void ProsperoAiApp::ImeResult(const char *text, void *user_data)
{
    auto *app = static_cast<ProsperoAiApp *>(user_data);
    if (!app || !text)
        return;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    if (!*text)
        return;
    std::snprintf(app->pending_text_, sizeof(app->pending_text_), "%s", text);
    app->pending_ = true;
    conversation_auto_follow = true;
    app->SetStatus("Preparing the GPU");
}

void ProsperoAiApp::PublishStream(const char *text)
{
    while (stream_lock_.test_and_set(std::memory_order_acquire))
    {
    }
    std::snprintf(stream_text_, sizeof(stream_text_), "%s", text ? text : "");
    stream_lock_.clear(std::memory_order_release);
    stream_version_.fetch_add(1, std::memory_order_release);
}

void ProsperoAiApp::CopyStream(char *text, std::size_t capacity)
{
    while (stream_lock_.test_and_set(std::memory_order_acquire))
    {
    }
    std::snprintf(text, capacity, "%s", stream_text_);
    stream_lock_.clear(std::memory_order_release);
}

void ProsperoAiApp::StreamCallback(const char *text)
{
    if (active_)
        active_->PublishStream(text);
}

void *ProsperoAiApp::GenerationWorker(void *user_data)
{
    auto *app = static_cast<ProsperoAiApp *>(user_data);
    gpt_runtime_message_t messages[MessageCapacity + 1]{};
    messages[0] = {"system", kSystemPrompts[app->style_]};
    const unsigned context_start = app->current_session_.context_start < app->history_count_
                                       ? app->current_session_.context_start
                                       : 0;
    unsigned message_count = 1;
    for (unsigned i = context_start; i < app->history_count_; ++i)
    {
        messages[message_count].role = app->history_[i].role;
        messages[message_count].content = app->history_[i].content;
        ++message_count;
    }
    const gpt_runtime_settings_t settings{app->style_, app->output_limit_};
    app->generation_result_ = gpt_runtime_generate(
        messages, message_count, settings, app->generation_response_,
        sizeof(app->generation_response_), &app->generation_stats_, StreamCallback);
    app->generation_done_.store(true, std::memory_order_release);
    return nullptr;
}

bool ProsperoAiApp::StartGeneration()
{
    if (!session_model_available_)
    {
        SetStatus("Install this session's model before continuing");
        return false;
    }
    PublishStream("");
    CurrentTimestamp(generation_timestamp_, sizeof(generation_timestamp_));
    generation_response_[0] = '\0';
    generation_done_.store(false, std::memory_order_relaxed);
    generating_ = true;
    pthread_attr_t attributes;
    int result = pthread_attr_init(&attributes);
    const bool attributes_initialized = result == 0;
    if (result == 0)
        result = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
    if (result == 0)
        result = scePthreadCreate(&generation_thread_, &attributes, GenerationWorker, this,
                                  "prosperoai-gpu-chat");
    if (attributes_initialized)
        pthread_attr_destroy(&attributes);
    if (result != 0)
    {
        generating_ = false;
        SetStatus("Could not start inference");
        return false;
    }
    const char *purpose = gpt_runtime_purpose();
    if (std::strcmp(purpose, "text-to-image") == 0)
        SetStatus("Generating image on the GPU");
    else if (std::strcmp(purpose, "text-to-audio") == 0)
        SetStatus("Generating audio  ·  10+ minutes");
    else if (std::strcmp(purpose, "text-to-speech") == 0)
        SetStatus("Generating speech on the GPU");
    else
        SetStatus(runtime_warm ? "Thinking on the GPU"
                               : "Loading selected model  ·  first reply may take a few seconds");
    return true;
}

void ProsperoAiApp::FinishGeneration()
{
    if (!generating_ || !generation_done_.load(std::memory_order_acquire))
        return;
    scePthreadJoin(generation_thread_, nullptr);
    generating_ = false;
    if (generation_result_ == 0)
    {
        runtime_warm = true;
        ArchiveGeneratedMedia();
        AddMessage("assistant", generation_response_);
        char status[96];
        const char *purpose = gpt_runtime_purpose();
#if defined(PS5_MEDIA_AUDIO)
        if (IsAudioPurpose(purpose))
        {
            char playback_path[320];
            const int playback = FindLatestAudio(playback_path, sizeof(playback_path))
                                     ? ps5_media_play_wav(playback_path)
                                     : 5;
            if (playback == 0)
            {
                std::snprintf(playing_audio_path, sizeof(playing_audio_path), "%s", playback_path);
                audio_animation_frame = BusyAnimationFrame();
            }
            char playback_log[80];
            std::snprintf(playback_log, sizeof(playback_log),
                          "[prosperoai] audio_playback=%u rc=%08X\n", playback == 0 ? 1U : 0U,
                          static_cast<unsigned>(playback));
            sceKernelDebugOutText(0, playback_log);
        }
#endif
        if (std::strcmp(purpose, "text-to-image") == 0)
            std::snprintf(status, sizeof(status), "Ready  ·  image generated  ·  %.1f seconds",
                          generation_stats_.elapsed_microseconds / 1000000.0);
        else if (std::strcmp(purpose, "text-to-audio") == 0)
            std::snprintf(status, sizeof(status), "Ready  ·  audio generated  ·  %.1f seconds",
                          generation_stats_.elapsed_microseconds / 1000000.0);
        else if (std::strcmp(purpose, "text-to-speech") == 0)
            std::snprintf(status, sizeof(status), "Ready  ·  speech generated  ·  %.1f seconds",
                          generation_stats_.elapsed_microseconds / 1000000.0);
        else if (gpt_runtime_available())
        {
            const std::uint64_t decode_microseconds =
                generation_stats_.elapsed_microseconds > generation_stats_.prefill_microseconds
                    ? generation_stats_.elapsed_microseconds -
                          generation_stats_.prefill_microseconds
                    : generation_stats_.elapsed_microseconds;
            const unsigned decode_tokens = generation_stats_.generated_tokens > 1
                                               ? generation_stats_.generated_tokens - 1
                                               : generation_stats_.generated_tokens;
            const double rate = decode_microseconds
                                    ? static_cast<double>(decode_tokens) * 1000000.0 /
                                          static_cast<double>(decode_microseconds)
                                    : 0.0;
            const unsigned context_tokens =
                generation_stats_.prompt_tokens + generation_stats_.generated_tokens - 1;
            std::snprintf(status, sizeof(status), "Ready  ·  %u tokens  ·  %.1f tok/s  ·  %u ctx",
                          generation_stats_.generated_tokens, rate, context_tokens);
        }
        else
            std::snprintf(status, sizeof(status), "UI template  ·  runtime hook ready");
        SetStatus(status);
        char log[256];
        std::snprintf(log, sizeof(log), "[prosperoai] runtime=%s generated=%u elapsed_us=%llu\n",
                      gpt_runtime_backend(), generation_stats_.generated_tokens,
                      static_cast<unsigned long long>(generation_stats_.elapsed_microseconds));
        sceKernelDebugOutText(0, log);
        sceKernelDebugOutText(0, "[prosperoai] response=");
        sceKernelDebugOutText(0, generation_response_);
        sceKernelDebugOutText(0, "\n");
#if defined(PS5_TEXT_REGRESSION_AUTOTEST)
        if (std::strcmp(gpt_runtime_purpose(), "text-to-text") == 0)
            sceKernelDebugOutText(0, "[prosperoai] text_regression_complete=1\n");
#endif
    }
    else
    {
        if (gpt_runtime_context_full() && current_session_.context_start + 2 < history_count_)
        {
            current_session_.context_start += 2;
            SaveSession();
            SetStatus("Trimming old conversation history");
            StartGeneration();
            return;
        }
        char log[160];
        std::snprintf(log, sizeof(log), "[prosperoai] runtime_failed rc=%08X\n",
                      static_cast<unsigned>(generation_result_));
        sceKernelDebugOutText(0, log);
        SetStatus(generation_response_[0] ? generation_response_
                                          : "Model runtime failed  ·  check the system log");
    }
    RefreshConversation();
#if defined(PS5_MEDIA_IMAGE_AUTOTEST)
    if (generation_result_ == 0 && std::strcmp(gpt_runtime_purpose(), "text-to-image") == 0)
        sceKernelDebugOutText(0, "[prosperoai] image_presented=1\n");
#endif
#if defined(PS5_MEDIA_SWITCH_AUTOTEST)
    if (generation_result_ == 0 && !automatic_media_switch_started &&
        std::strcmp(gpt_runtime_purpose(), "text-to-image") == 0)
    {
        for (unsigned index = 0; index < gpt_runtime_model_count(); ++index)
        {
            if (std::strcmp(gpt_runtime_model_purpose(index), "text-to-text") == 0)
            {
                automatic_media_switch_started = true;
                SelectModel(index);
                if (std::strcmp(gpt_runtime_purpose(), "text-to-text") == 0)
                {
                    std::snprintf(pending_text_, sizeof(pending_text_), "Hello");
                    pending_ = true;
                    SetStatus("Automatic image-to-text switch queued");
                }
                break;
            }
        }
    }
    else if (generation_result_ == 0 && automatic_media_switch_started &&
             std::strcmp(gpt_runtime_purpose(), "text-to-text") == 0)
    {
        sceKernelDebugOutText(0, "[prosperoai] media_switch_complete=1\n");
    }
#endif
}

void ProsperoAiApp::ArchiveGeneratedMedia()
{
    if (!current_session_.id[0])
        return;
    const char *purpose = gpt_runtime_purpose();
    const char *kind = nullptr;
    const char *source = nullptr;
    if (std::strcmp(purpose, "text-to-image") == 0)
    {
        kind = "image";
        source = std::strstr(generation_response_, "/download0/");
    }
    else if (IsAudioPurpose(purpose))
    {
        kind = "audio";
        source = "/download0/prosperoai-audio.wav";
    }
    if (!kind || !source)
        return;
    char source_path[256]{};
    if (source == generation_response_ || std::strstr(generation_response_, source))
    {
        std::size_t length = 0;
        while (source[length] && source[length] != '\r' && source[length] != '\n' &&
               length + 1 < sizeof(source_path))
            ++length;
        std::memcpy(source_path, source, length);
        source_path[length] = '\0';
    }
    else
    {
        std::snprintf(source_path, sizeof(source_path), "%s", source);
    }
    char archived[320]{};
    if (!prospero_session::archive_media(current_session_.id, history_count_, kind, source_path,
                                         archived, sizeof(archived)))
    {
        sceKernelDebugOutText(0, "[prosperoai] session_media_archived=0\n");
        return;
    }
    std::snprintf(generation_response_, sizeof(generation_response_), "%s generated locally.\n%s",
                  std::strcmp(kind, "image") == 0 ? "Image" : "Audio", archived);
    sceKernelDebugOutText(0, "[prosperoai] session_media_archived=1\n");
}

void ProsperoAiApp::Poll()
{
    if (warming && warmup_done.load(std::memory_order_acquire))
    {
        scePthreadJoin(warmup_thread, nullptr);
        warming = false;
        runtime_warm = warmup_result == 0;
        SetStatus(runtime_warm ? "Ready  ·  model loaded" : "Background model load failed");
        sceKernelDebugOutText(0, runtime_warm ? "[prosperoai] warmup_complete=1\n"
                                              : "[prosperoai] warmup_complete=0\n");
        RefreshSettings();
    }
    if (pending_ && !generating_ && !warming)
    {
        if (!gpt_runtime_available())
        {
            pending_ = false;
            pending_text_[0] = '\0';
            SetStatus("No models found  ·  add one to PPSA99004/models/");
            RefreshConversation();
            return;
        }
        conversation_auto_follow = true;
        const unsigned previous_count = history_count_;
        AddMessage("user", pending_text_);
        pending_text_[0] = '\0';
        pending_ = false;
        RefreshConversation();
        if (history_count_ > previous_count)
            StartGeneration();
    }
    const unsigned version = stream_version_.load(std::memory_order_acquire);
    if (version != displayed_stream_version_)
    {
        displayed_stream_version_ = version;
        RefreshConversation();
        RefreshStatus();
    }
    FinishGeneration();
#if defined(PS5_MEDIA_AUDIO)
    const bool audio_playing = ps5_media_is_playing() != 0;
    const unsigned next_audio_frame = audio_playing ? BusyAnimationFrame() : 4;
    if (next_audio_frame != audio_animation_frame)
    {
        audio_animation_frame = next_audio_frame;
        if (!audio_playing)
            playing_audio_path[0] = '\0';
        RefreshConversation();
    }
#endif
    if (automatic_chat_file && !generating_ && !warming && !pending_ && history_count_ >= 2)
    {
        if (std::fgets(pending_text_, sizeof(pending_text_), automatic_chat_file))
        {
            pending_text_[std::strcspn(pending_text_, "\r\n")] = '\0';
            pending_ = pending_text_[0] != '\0';
        }
        else
        {
            std::fclose(automatic_chat_file);
            automatic_chat_file = nullptr;
        }
    }
    if (warming || generating_)
    {
        const unsigned frame = BusyAnimationFrame();
        if (frame != busy_animation_frame)
        {
            busy_animation_frame = frame;
            RefreshStatus();
        }
    }
    else
    {
        busy_animation_frame = 4;
    }
    if (automatic_chat_test && !generating_ && history_count_ >= 2)
    {
        ++automatic_scroll_frame;
        if (automatic_scroll_frame == 3)
        {
            ScrollConversation(-100000.0f);
            RefreshConversation();
        }
        else if (automatic_scroll_frame == 6)
        {
            ScrollConversation(100000.0f);
            RefreshConversation();
        }
    }
}

void ProsperoAiApp::SetView(View view)
{
    view_ = view;
    SetVisible(document_, "conversation-screen", view_ == View::Conversation);
    SetVisible(document_, "workshop-screen", view_ == View::Workshop);
    SetClass(document_, "tab-conversation", "active", view_ == View::Conversation);
    SetClass(document_, "tab-workshop", "active", view_ == View::Workshop);
    RefreshSettings();
}

void ProsperoAiApp::ChangeSetting(int direction)
{
    if (generating_ || warming)
        return;
    if (settings_focus_ == 0)
    {
        const unsigned count = gpt_runtime_model_count();
        if (count > 1)
        {
            const int current = static_cast<int>(gpt_runtime_selected_model());
            SelectModel(static_cast<unsigned>((current + direction + static_cast<int>(count)) %
                                              static_cast<int>(count)));
        }
        return;
    }
    if (settings_focus_ == 1)
    {
        style_ = static_cast<unsigned>((static_cast<int>(style_) + direction + 3) % 3);
    }
    else
    {
        int index = static_cast<int>(OutputLimitIndex(output_limit_));
        index = (index + direction + 4) % 4;
        output_limit_ = kOutputLimits[index];
    }
    SaveSettings();
    RefreshSettings();
}

void ProsperoAiApp::HandleInput(const gpt_input_event_t &event)
{
    if (!event.pressed)
        return;
    if (event.key == GPT_INPUT_OPTIONS)
    {
        SetView(view_ == View::Conversation ? View::Workshop : View::Conversation);
        return;
    }
    if (event.key == GPT_INPUT_L1)
    {
        SetView(View::Conversation);
        return;
    }
    if (event.key == GPT_INPUT_R1)
    {
        SetView(View::Workshop);
        return;
    }
    if (view_ == View::Conversation)
    {
        if (event.key == GPT_INPUT_SCROLL_UP)
        {
            ScrollConversation(-kConversationScrollStep);
            RefreshConversation();
            return;
        }
        if (event.key == GPT_INPUT_SCROLL_DOWN)
        {
            ScrollConversation(kConversationScrollStep);
            RefreshConversation();
            return;
        }
        if (event.key == GPT_INPUT_UP || event.key == GPT_INPUT_DOWN)
        {
            session_rail_focused_ = true;
            const unsigned choices = session_count_ + 1;
            session_selection_ = event.key == GPT_INPUT_UP
                                     ? (session_selection_ + choices - 1) % choices
                                     : (session_selection_ + 1) % choices;
            RefreshSessions();
            return;
        }
        if (event.key == GPT_INPUT_LEFT)
        {
            session_rail_focused_ = true;
            session_selection_ = 0;
            for (unsigned index = 0; index < session_count_; ++index)
                if (current_session_.id[0] &&
                    std::strcmp(current_session_.id, sessions_[index].id) == 0)
                    session_selection_ = index + 1;
            RefreshSessions();
            return;
        }
        if (event.key == GPT_INPUT_RIGHT || event.key == GPT_INPUT_CIRCLE)
        {
            session_rail_focused_ = false;
            RefreshSessions();
            return;
        }
        if (event.key == GPT_INPUT_SQUARE)
        {
            if (!generating_ && !warming)
                BeginNewSession();
            return;
        }
        if (event.key == GPT_INPUT_TRIANGLE)
        {
            if (session_rail_focused_)
                DeleteSelectedSession();
            else
                PlayLatestAudio();
            return;
        }
        if (event.key == GPT_INPUT_CROSS && session_rail_focused_)
        {
            OpenSelectedSession();
            return;
        }
        if ((event.key == GPT_INPUT_TEXT || event.key == GPT_INPUT_BACKSPACE ||
             event.key == GPT_INPUT_ENTER || event.key == GPT_INPUT_CROSS) &&
            (!gpt_runtime_available() || !session_model_available_))
        {
            SetStatus(!gpt_runtime_available() ? "No models found  ·  add one to PPSA99004/models/"
                                               : "Install this session's model before continuing");
            return;
        }
        if (event.key == GPT_INPUT_TEXT && !gpt_ime_active() && !generating_ && !pending_)
        {
            session_rail_focused_ = false;
            RefreshSessions();
            const std::size_t length = std::strlen(pending_text_);
            if (event.text && length + 1 < sizeof(pending_text_))
            {
                pending_text_[length] = event.text;
                pending_text_[length + 1] = '\0';
                SetText(document_, "composer-placeholder", pending_text_);
                SetClass(document_, "composer-placeholder", "draft", true);
            }
        }
        else if (event.key == GPT_INPUT_BACKSPACE && !gpt_ime_active() && !generating_ && !pending_)
        {
            const std::size_t length = std::strlen(pending_text_);
            if (length)
                pending_text_[length - 1] = '\0';
            SetText(document_, "composer-placeholder",
                    pending_text_[0] ? pending_text_ : kComposerPrompt);
            SetClass(document_, "composer-placeholder", "draft", pending_text_[0] != '\0');
        }
        else if (event.key == GPT_INPUT_ENTER && !gpt_ime_active() && !generating_ && !pending_ &&
                 pending_text_[0])
        {
            pending_ = true;
            conversation_auto_follow = true;
            SetStatus("Preparing the GPU");
        }
        else if (event.key == GPT_INPUT_CROSS)
            OpenKeyboard();
        return;
    }
    if (event.key == GPT_INPUT_CIRCLE)
    {
        SetView(View::Conversation);
    }
    else if (event.key == GPT_INPUT_UP || event.key == GPT_INPUT_DOWN)
    {
        settings_focus_ =
            event.key == GPT_INPUT_UP ? (settings_focus_ + 2U) % 3U : (settings_focus_ + 1U) % 3U;
        RefreshSettings();
    }
    else if (event.key == GPT_INPUT_LEFT)
    {
        ChangeSetting(-1);
    }
    else if (event.key == GPT_INPUT_RIGHT || event.key == GPT_INPUT_CROSS)
    {
        ChangeSetting(1);
    }
    else if (event.key == GPT_INPUT_SQUARE && !generating_ && !warming)
    {
        style_ = 0;
        output_limit_ = 128;
        SelectModel(0);
        SaveSettings();
        RefreshSettings();
    }
}

void ProsperoAiApp::RefreshConversation()
{
    char partial[sizeof(stream_text_)]{};
    if (generating_)
        CopyStream(partial, sizeof(partial));
    const bool have_partial = generating_ && partial[0];
    const unsigned total = history_count_ + (have_partial ? 1U : 0U);
    const bool available = gpt_runtime_available();
    RefreshAudioAction();
    SetText(document_, "empty-title", available ? "The room is yours." : "No models found.");
    SetText(document_, "empty-copy",
            available
                ? "Press Cross to open the PS5 keyboard\nand begin a private, local conversation."
                : "Copy a curated model folder to PPSA99004/models/\nand restart ProsperoAI.");
    SetVisible(document_, "empty-state", total == 0);
    SetVisible(document_, "message-list", total != 0);
    int heights[MessageCapacity + 1]{};
    for (unsigned slot = 0; slot < MessageCapacity + 1; ++slot)
    {
        char row_id[24];
        char role_id[28];
        char time_id[28];
        char content_id[32];
        std::snprintf(row_id, sizeof(row_id), "message-%u", slot);
        std::snprintf(role_id, sizeof(role_id), "message-role-%u", slot);
        std::snprintf(time_id, sizeof(time_id), "message-time-%u", slot);
        std::snprintf(content_id, sizeof(content_id), "message-content-%u", slot);
        const bool visible = slot < total;
        SetVisible(document_, row_id, visible);
        if (!visible)
            continue;
        const bool stream_row = slot == history_count_;
        const Message *message = stream_row ? nullptr : &history_[slot];
        const bool user = message && std::strcmp(message->role, "user") == 0;
        const char *content = stream_row ? partial : message->content;
        SetPixelProperty(document_, row_id, "height", kMessageMinimumHeight);
        SetClass(document_, row_id, "user", user);
        SetClass(document_, row_id, "assistant", !user);
        SetText(document_, role_id, user ? "YOU" : "PROSPEROAI");
        SetText(document_, time_id, stream_row ? generation_timestamp_ : message->timestamp);
        const unsigned line_count = SetMessageText(document_, content_id, content);
        heights[slot] = static_cast<int>(line_count) * 26 + 48;
        if (heights[slot] < kMessageMinimumHeight)
            heights[slot] = kMessageMinimumHeight;
    }
    conversation_content_height = 0;
    for (unsigned slot = 0; slot < total; ++slot)
    {
        conversation_content_height += heights[slot];
        if (slot + 1 < total)
            conversation_content_height += kMessageGap;
    }
    const int maximum = ConversationScrollMaximum();
    if (conversation_auto_follow)
        conversation_scroll_offset = maximum;
    else if (conversation_scroll_offset > maximum)
        conversation_scroll_offset = maximum;

    int message_top = 0;
    for (unsigned slot = 0; slot < total; ++slot)
    {
        char row_id[24];
        char content_id[32];
        std::snprintf(row_id, sizeof(row_id), "message-%u", slot);
        std::snprintf(content_id, sizeof(content_id), "message-content-%u", slot);
        SetPixelProperty(document_, row_id, "top", message_top);
        SetPixelProperty(document_, row_id, "height", heights[slot]);
        message_top += heights[slot] + kMessageGap;
    }
    SetPixelProperty(document_, "message-scroll-end", "height",
                     conversation_content_height > 0 ? conversation_content_height : 1);
    document_->UpdateDocument();
    if (Rml::Element *list = Find(document_, "message-list"))
        list->SetScrollTop(conversation_scroll_offset);
    SetText(document_, "composer-placeholder", pending_text_[0] ? pending_text_ : kComposerPrompt);
    SetClass(document_, "composer-placeholder", "draft", pending_text_[0] != '\0');
}

void ProsperoAiApp::BuildMessageRows()
{
    Rml::Element *list = Find(document_, "message-list");
    if (!list)
        return;
    Rml::String markup;
    markup.reserve(22000);
    for (unsigned slot = 0; slot < MessageCapacity + 1; ++slot)
    {
        char row[512];
        std::snprintf(row, sizeof(row),
                      "<div id=\"message-%u\" class=\"message assistant hidden\">"
                      "<span id=\"message-role-%u\" class=\"message-role\">PROSPEROAI</span>"
                      "<span id=\"message-time-%u\" class=\"message-time\"></span>"
                      "<div id=\"message-content-%u\" class=\"message-content\"></div></div>",
                      slot, slot, slot, slot);
        markup += row;
    }
    markup += "<span id=\"message-scroll-end\"></span>";
    list->SetInnerRML(markup);
}

void ProsperoAiApp::RefreshStatus()
{
    constexpr const char *spinner[] = {"|", "/", "-", "\\"};
    if (warming || generating_)
    {
        constexpr const char *dots[] = {"", ".", "..", "..."};
        char animated[128];
        const unsigned frame = BusyAnimationFrame();
        std::snprintf(animated, sizeof(animated), "%s%s", status_text_, dots[frame]);
        SetText(document_, "status-label", animated);
        busy_animation_frame = frame;
    }
    else
    {
        SetText(document_, "status-label", status_text_);
    }
    char metric[96];
    const char *purpose = gpt_runtime_purpose();
    const bool media = IsMediaPurpose(purpose);
    if (warming)
    {
        std::snprintf(metric, sizeof(metric), "Loading model into GPU memory");
        SetText(document_, "model-state", "LOADING TO GPU");
    }
    else if (generating_)
    {
        std::snprintf(metric, sizeof(metric), "Generating %s on the PS5 GPU",
                      std::strcmp(purpose, "text-to-image") == 0    ? "image"
                      : std::strcmp(purpose, "text-to-audio") == 0  ? "audio"
                      : std::strcmp(purpose, "text-to-speech") == 0 ? "speech"
                                                                    : "text");
        SetText(document_, "model-state", "GENERATING");
    }
    else
    {
        const bool available = gpt_runtime_available() && session_model_available_;
        std::snprintf(metric, sizeof(metric), "%s",
                      available                ? media          ? "Ready; weights load with the first prompt"
                                                 : runtime_warm ? "Model loaded in GPU memory"
                                                                : "Model selected"
                      : current_session_.id[0] ? "This session's model is not installed"
                                               : "Add a model to PPSA99004/models/");
        SetText(document_, "model-state",
                available                ? media          ? "READY - LOADS ON USE"
                                           : runtime_warm ? "LOADED AND READY"
                                                          : "SELECTED"
                : current_session_.id[0] ? "MODEL NOT INSTALLED"
                                         : "NO MODELS FOUND");
    }
    SetText(document_, "context-label", metric);
    const bool busy = warming || generating_;
    SetText(document_, "model-spinner", busy ? spinner[busy_animation_frame & 3U] : "");
    SetClass(document_, "model-spinner", "busy", busy);
    SetClass(document_, "model-state", "busy", busy);
}

void ProsperoAiApp::RefreshSettings()
{
    SetText(document_, "selected-model-value", gpt_runtime_name());
    char installed[160];
    const unsigned count = gpt_runtime_model_count();
    const char *purpose = gpt_runtime_purpose();
    const bool media = IsMediaPurpose(purpose);
    if (count)
    {
        const unsigned position = gpt_runtime_selected_model() + 1;
        const char *state = warming        ? "Loading into GPU memory"
                            : generating_  ? "Generating now"
                            : media        ? "Ready; loads on first prompt"
                            : runtime_warm ? "Loaded in GPU memory"
                                           : "Selected";
        std::snprintf(installed, sizeof(installed), "%s | Model %u of %u | %s",
                      PurposeLabel(purpose), position, count, state);
        SetText(document_, "selected-model-load-state",
                warming        ? "LOADING"
                : generating_  ? "GENERATING"
                : media        ? "READY ON USE"
                : runtime_warm ? "LOADED"
                               : "SELECTED");
    }
    else
    {
        std::snprintf(installed, sizeof(installed), "Copy model folders to PPSA99004/models/");
        SetText(document_, "selected-model-load-state", "NO MODELS");
    }
    SetText(document_, "selected-model-description", installed);
    SetText(document_, "runtime-note",
            std::strcmp(purpose, "text-to-image") == 0
                ? "Return to Conversation, write an image prompt, and press X.\nThe image is saved "
                  "with the session."
            : std::strcmp(purpose, "text-to-audio") == 0
                ? "Return to Conversation, describe the audio, and press X.\nPlayback starts when "
                  "generation finishes."
            : std::strcmp(purpose, "text-to-speech") == 0
                ? "Return to Conversation, enter text to speak, and press X.\nPlayback starts when "
                  "synthesis finishes."
                : "Return to Conversation, write a message, and press X.\nThe model continues the "
                  "selected session.");
    SetText(document_, "style-value", kStyleNames[style_]);
    SetText(document_, "style-description", kStyleDescriptions[style_]);
    char value[32];
    std::snprintf(value, sizeof(value), "%u tokens", output_limit_);
    SetText(document_, "tokens-value", value);
    if (media)
        settings_focus_ = 0;
    SetVisible(document_, "setting-style", !media);
    SetVisible(document_, "setting-tokens", !media);
    SetClass(document_, "setting-model", "focused", settings_focus_ == 0);
    SetClass(document_, "selected-model-load-state", "busy", warming || generating_);
    SetClass(document_, "setting-style", "focused", settings_focus_ == 1);
    SetClass(document_, "setting-tokens", "focused", settings_focus_ == 2);
}

void ProsperoAiApp::RefreshAll()
{
    const bool missing_session_model = current_session_.id[0] && !session_model_available_;
    const char *display_name =
        missing_session_model ? current_session_.model_name : gpt_runtime_name();
    SetText(document_, "model-name", display_name);
    char rail[128];
    std::snprintf(rail, sizeof(rail), "%s\n%s", display_name,
                  missing_session_model ? "Model not installed" : gpt_runtime_backend());
    SetText(document_, "rail-note", rail);
    const char *purpose = gpt_runtime_purpose();
    SetText(document_, "runtime-name", PurposeLabel(purpose));
    SetText(document_, "runtime-backend", "Native AGC GPU");
    SetText(document_, "runtime-contract",
            std::strcmp(purpose, "text-to-image") == 0    ? "Prompt + image"
            : std::strcmp(purpose, "text-to-audio") == 0  ? "Prompt + audio"
            : std::strcmp(purpose, "text-to-speech") == 0 ? "Text + speech"
                                                          : "Chat messages");
    SetView(view_);
    RefreshSessions();
    RefreshConversation();
    RefreshStatus();
    RefreshSettings();
}

void ProsperoAiApp::LoadSettings()
{
    style_ = 0;
    output_limit_ = 128;
    std::FILE *file = std::fopen(kSettingsPath, "rb");
    unsigned style = 0;
    unsigned limit = 128;
    unsigned model = 0;
    if (file)
    {
        const int fields = std::fscanf(file, "%u %u %u", &style, &limit, &model);
        if (fields >= 2 && style < 3 && IsOutputLimit(limit))
        {
            style_ = style;
            output_limit_ = limit;
            if (fields == 3)
                gpt_runtime_select_model(model);
        }
        std::fclose(file);
    }
#if defined(PS5_QWEN_DIAGNOSTIC_PHASES) || defined(PS5_QWEN_CONTEXT_DIAGNOSTIC_PHASES) ||          \
    defined(PS5_QWEN_SMOKE_TEST) || defined(PS5_LOGITS_DIAGNOSTIC)
    gpt_runtime_select_model(0);
#endif
}

void ProsperoAiApp::SaveSettings() const
{
    if (std::FILE *file = std::fopen(kSettingsPath, "wb"))
    {
        std::fprintf(file, "%u %u %u\n", style_, output_limit_, gpt_runtime_selected_model());
        std::fclose(file);
    }
}
