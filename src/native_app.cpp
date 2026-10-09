// ProsperoAI native application controller.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_app.hpp"

#include "debug_log.hpp"
#ifdef PS5_LLAMA_VULKAN
#include "http_server.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sys/time.h>

#ifndef PROSPERO_SETTINGS_PATH
#define PROSPERO_SETTINGS_PATH "/download0/prosperoai.cfg"
#endif
#ifndef PROSPERO_HOST
extern "C" int scePthreadCreate(void **, const void *, void *(*)(void *), void *, const char *);
extern "C" int scePthreadJoin(void *, void **);
extern "C" int ps5_agc_backend_reserve();
#endif
#ifdef PS5_MEDIA_AUDIO
extern "C" int ps5_media_play_wav(const char *);
extern "C" void ps5_media_shutdown();
#endif

namespace prospero
{
namespace
{
constexpr const char *kPrompts[] = {
    "You are ProsperoAI, a thoughtful AI assistant running locally on a PlayStation 5. Be clear, "
    "helpful, and concise.",
    "You are ProsperoAI, a precise AI assistant running locally on a PlayStation 5. Answer "
    "directly with concise factual language and state uncertainty.",
    "You are ProsperoAI, a creative AI assistant running locally on a PlayStation 5. Explore "
    "imaginative options while staying grounded, helpful, and concise."};

std::string truncate_utf8(const std::string &text, std::size_t capacity)
{
    std::size_t end = std::min(text.size(), capacity);
    if (end < text.size())
        while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            --end;
    return text.substr(0, end);
}

prospero_session::Message message(const char *role, const std::string &text)
{
    prospero_session::Message value{};
    std::snprintf(value.role, sizeof(value.role), "%s", role);
    const auto content = truncate_utf8(text, sizeof(value.content) - 1);
    std::memcpy(value.content, content.c_str(), content.size() + 1);
    timeval now{};
    struct timezone zone
    {
    };
    gettimeofday(&now, &zone);
    const auto seconds = (static_cast<long long>(now.tv_sec) - zone.tz_minuteswest * 60LL) % 86400;
    std::snprintf(value.timestamp, sizeof(value.timestamp), "%02lld:%02lld", seconds / 3600,
                  seconds / 60 % 60);
    return value;
}

void validate(Preferences &p)
{
    p.style = std::min(p.style, 2U);
    if (p.output_limit != 64 && p.output_limit != 128 && p.output_limit != 192 &&
        p.output_limit != 256)
        p.output_limit = 128;
    p.theme = std::min(p.theme, 1U);
    p.accent = std::min(p.accent, 2U);
    p.volume = std::min(p.volume, 100U);
    p.text_size = std::min(p.text_size, 1U);
}

void load_preferences(Preferences &p, const std::vector<Model> &models)
{
    std::FILE *file = std::fopen(PROSPERO_SETTINGS_PATH, "rb");
    if (!file)
        return;
    char line[256]{};
    unsigned legacy_model = 0;
    if (std::fgets(line, sizeof(line), file) &&
        std::sscanf(line, "%u %u %u", &p.style, &p.output_limit, &legacy_model) == 3)
    {
        if (legacy_model < models.size())
            p.model_id = models[legacy_model].id;
    }
    else
    {
        std::rewind(file);
        while (std::fgets(line, sizeof(line), file))
        {
            char key[32]{}, value[128]{};
            if (std::sscanf(line, "%31[^=]=%127[^\r\n]", key, value) != 2)
                continue;
            if (std::strcmp(key, "model") == 0)
            {
                if (std::strspn(
                        value,
                        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") ==
                    std::strlen(value))
                    p.model_id = value;
                continue;
            }
            unsigned number = 0;
            if (std::sscanf(value, "%u", &number) != 1)
                continue;
            if (std::strcmp(key, "style") == 0)
                p.style = number;
            else if (std::strcmp(key, "tokens") == 0)
                p.output_limit = number;
            else if (std::strcmp(key, "theme") == 0)
                p.theme = number;
            else if (std::strcmp(key, "accent") == 0)
                p.accent = number;
            else if (std::strcmp(key, "volume") == 0)
                p.volume = number;
            else if (std::strcmp(key, "text_size") == 0)
                p.text_size = number;
            else if (std::strcmp(key, "reduced_motion") == 0)
                p.reduced_motion = number != 0;
            else if (std::strcmp(key, "high_contrast") == 0)
                p.high_contrast = number != 0;
        }
    }
    std::fclose(file);
    validate(p);
}

bool save_preferences(const Preferences &p)
{
    constexpr const char *temporary = PROSPERO_SETTINGS_PATH ".tmp";
    std::FILE *file = std::fopen(temporary, "wb");
    if (!file)
        return false;
    const int bytes =
        std::fprintf(file,
                     "version=2\nmodel=%s\nstyle=%u\ntokens=%u\ntheme=%u\naccent=%u\nvolume=%"
                     "u\ntext_size=%u\nreduced_motion=%u\nhigh_contrast=%u\n",
                     p.model_id.c_str(), p.style, p.output_limit, p.theme, p.accent, p.volume,
                     p.text_size, p.reduced_motion ? 1U : 0U, p.high_contrast ? 1U : 0U);
    const int closed = std::fclose(file);
    if (bytes < 0 || closed != 0 || std::rename(temporary, PROSPERO_SETTINGS_PATH) != 0)
    {
        std::remove(temporary);
        return false;
    }
    return true;
}

int find_model(const State &state, const char *id)
{
    for (std::size_t i = 0; i < state.models.size(); ++i)
        if (state.models[i].id == id)
            return static_cast<int>(i);
    return -1;
}

std::string media_path(const char *text, const char *suffix)
{
    const char *start = std::strstr(text, "/download0/");
    const char *end = start ? std::strstr(start, suffix) : nullptr;
    if (!end || (std::strchr(start, '\n') && std::strchr(start, '\n') < end))
        return {};
    return std::string(start, static_cast<std::size_t>(end - start) + std::strlen(suffix));
}
} // namespace

App *App::active_ = nullptr;
App::~App()
{
    shutdown();
}

bool App::initialize()
{
    if (active_ && active_ != this)
        return false;
    active_ = this;
    return start(Job::Discover);
}

bool App::start(Job job)
{
    if (running_)
        return false;
    result_ = state_;
    result_.stream.clear();
    result_.notice = Notice::None;
    job_ = job;
    done_.store(false, std::memory_order_relaxed);
    stream_buffer_[0] = '\0';
    displayed_stream_ = stream_version_.load(std::memory_order_relaxed);
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    const bool initialized = error == 0;
    if (!error)
        error = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
    if (!error)
    {
#ifdef PROSPERO_HOST
        error = pthread_create(&thread_, &attributes, worker, this);
#else
        error = scePthreadCreate(&thread_, &attributes, worker, this, "prospero-worker");
#endif
    }
    if (initialized)
        pthread_attr_destroy(&attributes);
    running_ = error == 0;
    if (!running_)
        state_.status = "Could not start background work. Please try again.";
    ++state_.revision;
    return running_;
}

void App::join()
{
#ifdef PROSPERO_HOST
    pthread_join(thread_, nullptr);
#else
    scePthreadJoin(thread_, nullptr);
#endif
    running_ = false;
}

bool App::refresh_models()
{
    return !busy() && start(Job::RefreshModels);
}

void App::shutdown()
{
    if (running_)
    {
        join();
        if (job_ == Job::Discover)
            state_ = std::move(result_);
        else
            state_.preferences.model_id = result_.preferences.model_id;
    }
    if (active_ == this && (preferences_dirty_ || job_ == Job::Select || job_ == Job::Open))
        save_preferences(state_.preferences);
    preferences_dirty_ = false;
#ifdef PS5_MEDIA_AUDIO
    ps5_media_shutdown();
#endif
    if (active_ == this)
        active_ = nullptr;
}

void App::poll()
{
    const unsigned version = stream_version_.load(std::memory_order_acquire);
    if (running_ && version != displayed_stream_ &&
        !stream_lock_.test_and_set(std::memory_order_acquire))
    {
        state_.stream = stream_buffer_;
        stream_lock_.clear(std::memory_order_release);
        displayed_stream_ = version;
        ++state_.revision;
    }
    if (running_ && done_.load(std::memory_order_acquire))
    {
        join();
        if (job_ != Job::Discover)
        {
            const auto model = result_.preferences.model_id;
            result_.preferences = state_.preferences;
            result_.preferences.model_id = model;
        }
        result_.revision = state_.revision + 1;
        state_ = std::move(result_);
        if (state_.notice != Notice::None)
            notices_.push_back(state_.notice);
        if (job_ == Job::Discover && !state_.models.empty())
        {
            const int saved = find_model(state_, state_.preferences.model_id.c_str());
            select_model(saved < 0 ? 0U : static_cast<unsigned>(saved));
        }
        else if (job_ == Job::Select || job_ == Job::Open)
            preferences_dirty_ = true;
    }
    if (!running_ && preferences_dirty_)
    {
        preferences_dirty_ = false;
        if (!start(Job::Preferences))
            preferences_dirty_ = true;
    }
}

bool App::generating() const
{
    return running_ && job_ == Job::Generate;
}
Activity App::activity() const
{
    if (!running_)
        return Activity::Idle;
    switch (job_)
    {
    case Job::Discover:
        return Activity::Starting;
    case Job::Select:
        return Activity::PreparingModel;
    case Job::Open:
        return Activity::OpeningConversation;
    case Job::Generate:
        return Activity::Generating;
    case Job::Save:
        return Activity::Saving;
    default:
        return Activity::Idle;
    }
}
std::vector<Notice> App::take_notices()
{
    std::vector<Notice> notices;
    notices.swap(notices_);
    return notices;
}
bool App::can_send() const
{
    return !running_ && state_.ready && state_.selected_model >= 0 &&
           (!state_.session.id[0] ||
            state_.models[static_cast<std::size_t>(state_.selected_model)].id ==
                state_.session.model_id);
}

bool App::select_model(unsigned index)
{
    if (running_ || state_.unsaved || index >= state_.models.size())
        return false;
    argument_ = index;
    state_.status = "Preparing " + state_.models[index].name + "...";
    return start(Job::Select);
}

bool App::open_session(unsigned index)
{
    if (running_ || state_.unsaved || index >= state_.sessions.size())
        return false;
    session_id_ = state_.sessions[index].id;
    state_.status = "Opening conversation...";
    return start(Job::Open);
}

bool App::delete_session(unsigned index)
{
    if (running_ || state_.unsaved || index >= state_.sessions.size())
        return false;
    session_id_ = state_.sessions[index].id;
    return start(Job::Delete);
}

bool App::new_session()
{
    if (running_ || state_.unsaved)
        return false;
    state_.session = {};
    state_.messages.clear();
    state_.images.clear();
    state_.stream.clear();
    state_.retry_available = false;
    state_.stats_valid = false;
    state_.status = state_.ready ? "Ready when you are" : "Choose a model to begin";
    ++state_.revision;
    if (!state_.ready && state_.selected_model >= 0)
        return select_model(static_cast<unsigned>(state_.selected_model));
    return true;
}

bool App::send(const std::string &text)
{
    if (!can_send())
        return false;
    if (text.find_first_not_of(" \r\n\t") == std::string::npos)
        return false;
    if (text.size() >= prospero_session::MessageBytes)
    {
        state_.status = "Message is too long. Please shorten it before sending.";
        ++state_.revision;
        return false;
    }
    // A message that never got its answer (the generation or its save failed) is
    // replaced by the new one: two user turns in a row break some chat templates.
    const bool unanswered = state_.retry_available && !state_.messages.empty() &&
                            std::strcmp(state_.messages.back().role, "user") == 0;
    if (state_.messages.size() + (unanswered ? 1 : 2) > prospero_session::MessageCapacity)
    {
        state_.status = "This conversation is full. Start a new conversation to continue.";
        ++state_.revision;
        return false;
    }
    const auto previous = state_.messages;
    const bool could_retry = state_.retry_available;
    if (unanswered)
        state_.messages.pop_back();
    state_.messages.push_back(message("user", text));
    state_.stream.clear();
    state_.retry_available = false;
    state_.status = "Creating on your console...";
    if (!start(Job::Generate))
    {
        state_.messages = previous;
        state_.retry_available = could_retry;
        return false;
    }
    return true;
}

bool App::retry()
{
    if (!can_send() || !state_.retry_available || state_.messages.empty())
        return false;
    state_.status = "Trying again...";
    if (state_.unsaved && std::strcmp(state_.messages.back().role, "assistant") == 0)
        return start(Job::Save);
    return start(Job::Generate);
}

void App::set_preferences(Preferences preferences)
{
    validate(preferences);
    preferences.model_id = state_.preferences.model_id;
    state_.preferences = std::move(preferences);
    preferences_dirty_ = true;
    ++state_.revision;
}

bool App::play_audio()
{
    return !running_ && start(Job::Play);
}

void *App::worker(void *context)
{
    auto *app = static_cast<App *>(context);
    app->work();
    app->done_.store(true, std::memory_order_release);
    return nullptr;
}

void App::stream_callback(const char *text)
{
    App *app = active_;
    if (!app || !text)
        return;
    // Coalesce progress updates if the render thread is copying one. Never block inference.
    if (app->stream_lock_.test_and_set(std::memory_order_acquire))
        return;
    std::snprintf(app->stream_buffer_, sizeof(app->stream_buffer_), "%s", text);
    app->stream_lock_.clear(std::memory_order_release);
    app->stream_version_.fetch_add(1, std::memory_order_release);
}

void App::refresh_sessions()
{
    result_.sessions.resize(prospero_session::CatalogCapacity);
    result_.sessions.resize(prospero_session::scan(result_.sessions.data(),
                                                   static_cast<unsigned>(result_.sessions.size())));
}

void App::refresh_images()
{
    std::vector<std::shared_ptr<const ImagePreview>> images;
    for (const auto &message : result_.messages)
    {
        if (std::strcmp(message.role, "assistant") != 0)
            continue;
        const auto path = media_path(message.content, ".tga");
        if (path.empty() || path.find("..") != std::string::npos ||
            (path.find("/download0/ProsperoAI/sessions/") != 0 &&
             path.find("/download0/prosperoai-image-") != 0))
            continue;
        const auto cached = std::find_if(result_.images.begin(), result_.images.end(),
                                         [&](const auto &image) { return image->path == path; });
        auto image = cached == result_.images.end() ? load_image_preview(path) : *cached;
        if (image)
            images.push_back(std::move(image));
    }
    result_.images = std::move(images);
}

void App::work()
{
    const auto began = std::chrono::steady_clock::now();
    const Job job = job_;
    switch (job_)
    {
    case Job::Discover:
    {
#ifdef PS5_LLAMA_VULKAN
        gpt_runtime_refresh_models();
#endif
        result_.models.clear();
        result_.model_counts = {};
        for (unsigned i = 0, count = gpt_runtime_model_count(); i < count; ++i)
        {
            Model model{gpt_runtime_model_id(i), gpt_runtime_model_name(i),
                        gpt_runtime_model_purpose(i)};
            if (model.purpose == "text-to-image")
                model.capability = Capability::Image;
            else if (model.purpose == "text-to-audio")
                model.capability = Capability::Audio;
            else if (model.purpose == "text-to-speech")
                model.capability = Capability::Voice;
            ++result_.model_counts[static_cast<std::size_t>(model.capability)];
            result_.models.push_back(std::move(model));
        }
        load_preferences(result_.preferences, result_.models);
        refresh_sessions();
        result_.initialized = true;
        result_.status =
            result_.models.empty() ? "No models are installed yet" : "Your library is ready";
        break;
    }
    case Job::RefreshModels:
    {
        gpt_runtime_refresh_models();
        const std::string selected =
            result_.selected_model >= 0
                ? result_.models[static_cast<std::size_t>(result_.selected_model)].id
                : std::string();
        result_.models.clear();
        result_.model_counts = {};
        result_.selected_model = -1;
        for (unsigned i = 0, count = gpt_runtime_model_count(); i < count; ++i)
        {
            Model model{gpt_runtime_model_id(i), gpt_runtime_model_name(i),
                        gpt_runtime_model_purpose(i)};
            if (model.purpose == "text-to-image")
                model.capability = Capability::Image;
            else if (model.purpose == "text-to-audio")
                model.capability = Capability::Audio;
            else if (model.purpose == "text-to-speech")
                model.capability = Capability::Voice;
            if (model.id == selected)
                result_.selected_model = static_cast<int>(i);
            ++result_.model_counts[static_cast<std::size_t>(model.capability)];
            result_.models.push_back(std::move(model));
        }
        break;
    }
    case Job::Select:
#if !defined(PROSPERO_HOST) && !defined(PS5_LLAMA_VULKAN)
        ps5_agc_backend_reserve();
#endif
        result_.ready = gpt_runtime_select_model(argument_) && gpt_runtime_prepare() == 0;
        result_.selected_model = static_cast<int>(gpt_runtime_selected_model());
        result_.preferences.model_id =
            result_.models[static_cast<std::size_t>(result_.selected_model)].id;
        if (result_.ready)
        {
            result_.session = {};
            result_.messages.clear();
            result_.images.clear();
            result_.retry_available = false;
            result_.stats_valid = false;
        }
        result_.notice = result_.ready ? Notice::ModelReady : Notice::ModelFailed;
        result_.status = result_.ready ? "Ready when you are"
                                       : "Model preparation failed. Select the model to retry.";
        break;
    case Job::Open:
    {
        prospero_session::Record record{};
        std::vector<prospero_session::Message> messages(prospero_session::MessageCapacity);
        if (!prospero_session::load(session_id_.c_str(), &record, messages.data(),
                                    static_cast<unsigned>(messages.size())))
        {
            result_.status =
                "Could not open this conversation. Your current conversation is unchanged.";
            break;
        }
        messages.resize(record.message_count);
        result_.session = record;
        result_.messages = std::move(messages);
        refresh_images();
        result_.retry_available = false;
        result_.stats_valid = false;
        const int index = find_model(result_, record.model_id);
        result_.ready = index >= 0 && gpt_runtime_select_model(static_cast<unsigned>(index)) &&
                        gpt_runtime_prepare() == 0;
        if (index >= 0)
        {
            result_.selected_model = static_cast<int>(gpt_runtime_selected_model());
            result_.preferences.model_id =
                result_.models[static_cast<std::size_t>(result_.selected_model)].id;
        }
        result_.notice = index < 0       ? Notice::ModelMissing
                         : result_.ready ? Notice::None
                                         : Notice::ModelFailed;
        result_.status =
            index < 0 ? "This model is missing. Your saved conversation is available to read."
            : result_.ready
                ? "Conversation restored"
                : "Conversation restored. Model preparation failed; try again from Models.";
        break;
    }
    case Job::Delete:
        if (!prospero_session::erase(session_id_.c_str()))
            result_.status = "Could not delete this conversation.";
        else
        {
            if (session_id_ == result_.session.id)
            {
                result_.session = {};
                result_.messages.clear();
                result_.images.clear();
                result_.stats_valid = false;
            }
            result_.notice = Notice::ConversationDeleted;
            result_.status = "Conversation deleted";
            refresh_sessions();
        }
        break;
    case Job::Generate:
        generate();
        break;
    case Job::Save:
        result_.unsaved = !prospero_session::save(&result_.session, result_.messages.data(),
                                                  static_cast<unsigned>(result_.messages.size()));
        result_.retry_available = result_.unsaved;
        result_.notice = result_.unsaved ? Notice::SaveFailed : Notice::None;
        result_.status =
            result_.unsaved
                ? "Could not save. Check storage, then retry before leaving this conversation."
                : "Saved on your console";
        refresh_sessions();
        break;
    case Job::Preferences:
        if (!save_preferences(result_.preferences))
            result_.notice = Notice::SettingsNotSaved;
        break;
    case Job::Play:
        result_.status = "No audio in this conversation";
#ifdef PS5_MEDIA_AUDIO
        for (auto it = result_.messages.rbegin(); it != result_.messages.rend(); ++it)
        {
            const auto path = media_path(it->content, ".wav");
            if (path.empty())
                continue;
            result_.status =
                ps5_media_play_wav(path.c_str()) == 0 ? "Playing audio" : "Audio playback failed";
            break;
        }
#endif
        break;
    }
    if (debug::enabled())
    {
        static constexpr const char *names[] = {
            "find models",         "refresh models", "choose model", "open conversation",
            "delete conversation", "answer",         "save",         "save settings",
            "play sound"};
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        const char *model =
            result_.selected_model >= 0
                ? result_.models[static_cast<std::size_t>(result_.selected_model)].id.c_str()
                : "-";
        debug::line(job == Job::Generate ? "answer" : "model",
                    "%s: %.2f s, model=%s ready=%d models=%zu notice=%d \"%s\"",
                    names[static_cast<int>(job)], seconds, model, result_.ready ? 1 : 0,
                    result_.models.size(), static_cast<int>(result_.notice),
                    result_.status.c_str());
        if (job == Job::Generate && result_.stats_valid)
            debug::line("answer", "tokens=%u prompt=%u prefill=%llu ms total=%llu ms",
                        result_.stats.generated_tokens, result_.stats.prompt_tokens,
                        static_cast<unsigned long long>(result_.stats.prefill_microseconds / 1000),
                        static_cast<unsigned long long>(result_.stats.elapsed_microseconds / 1000));
        if (job == Job::Discover || job == Job::RefreshModels)
            for (const Model &found : result_.models)
                debug::line("model", "found %s (%s)", found.id.c_str(), found.purpose.c_str());
    }
}

void App::generate()
{
    const auto &model = result_.models[static_cast<std::size_t>(result_.selected_model)];
    result_.retry_available = true;
    result_.unsaved = true;
    result_.stats_valid = false;
    result_.notice = Notice::SaveFailed;
    if (!result_.session.id[0])
    {
        if (!prospero_session::create(&result_.session, model.id.c_str(), model.name.c_str(),
                                      model.purpose.c_str()))
        {
            result_.status =
                "Could not create a conversation. Check available storage, then retry.";
            return;
        }
        const auto title =
            truncate_utf8(result_.messages.front().content, sizeof(result_.session.title) - 1);
        std::snprintf(result_.session.title, sizeof(result_.session.title), "%s", title.c_str());
    }
    if (!prospero_session::save(&result_.session, result_.messages.data(),
                                static_cast<unsigned>(result_.messages.size())))
    {
        result_.status = "Could not save your message. Check available storage, then retry.";
        return;
    }
    char response[4096]{};
    result_.unsaved = false;
    int status = 1;
    do
    {
        std::vector<gpt_runtime_message_t> context{{"system", kPrompts[result_.preferences.style]}};
        for (std::size_t i = result_.session.context_start; i < result_.messages.size(); ++i)
            context.push_back({result_.messages[i].role, result_.messages[i].content});
        status = gpt_runtime_generate(context.data(), static_cast<unsigned>(context.size()),
                                      {result_.preferences.style, result_.preferences.output_limit},
                                      response, sizeof(response), &result_.stats, stream_callback);
        if (!status || !gpt_runtime_context_full() ||
            result_.session.context_start + 2 >= result_.messages.size())
            break;
        result_.session.context_start += 2;
    } while (true);
    result_.stream.clear();
    if (status)
    {
        result_.notice = Notice::GenerationFailed;
        result_.status =
            response[0] ? response : "Generation failed. Your message is saved; you can retry.";
        refresh_sessions();
        return;
    }
    bool archived = true;
    if (model.capability != Capability::Text)
    {
        const bool image = model.capability == Capability::Image;
        const auto source =
            image ? media_path(response, ".tga") : std::string("/download0/prosperoai-audio.wav");
        char path[320]{};
        archived = !source.empty() &&
                   prospero_session::archive_media(
                       result_.session.id, static_cast<unsigned>(result_.messages.size()),
                       image ? "image" : "audio", source.c_str(), path, sizeof(path));
        if (archived)
            std::snprintf(response, sizeof(response), "%s generated locally.\n%s",
                          image ? "Image" : "Audio", path);
    }
    result_.messages.push_back(message("assistant", response));
    refresh_images();
    const bool saved = prospero_session::save(&result_.session, result_.messages.data(),
                                              static_cast<unsigned>(result_.messages.size()));
    result_.unsaved = !saved;
    result_.retry_available = !saved;
    result_.stats_valid = true;
    result_.stats_kind = model.capability;
    result_.notice = !saved                                 ? Notice::SaveFailed
                     : model.capability == Capability::Text ? Notice::ReplyReady
                                                            : Notice::MediaReady;
    result_.status = !saved ? "Response is shown, but could not be saved. Check available storage."
                     : !archived ? "Created, but the media could not be archived. It may be "
                                   "replaced by the next generation."
                                 : "Saved on your console";
    refresh_sessions();
}
} // namespace prospero
