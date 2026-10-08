// ProsperoAI application state. Rendering never calls the model runtime or storage.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpt_runtime.hpp"
#include "session_store.hpp"
#include "media_preview.hpp"
#include <atomic>
#include <string>
#include <vector>
#ifdef PROSPERO_HOST
#include <pthread.h>
#endif

namespace prospero
{
enum class Capability
{
    Text,
    Image,
    Audio,
    Voice
};
struct Model
{
    std::string id, name, purpose;
    Capability capability = Capability::Text;
};
struct Preferences
{
    unsigned style = 0, output_limit = 128;
    unsigned theme = 0, accent = 0, volume = 35, text_size = 0;
    bool reduced_motion = false, high_contrast = false;
    std::string model_id;
};
struct State
{
    std::vector<Model> models;
    std::vector<prospero_session::Record> sessions;
    std::vector<prospero_session::Message> messages;
    std::vector<std::shared_ptr<const ImagePreview>> images;
    prospero_session::Record session{};
    Preferences preferences;
    int selected_model = -1;
    bool ready = false, initialized = false, retry_available = false, unsaved = false;
    std::string status = "Opening your space...", stream;
    gpt_runtime_stats_t stats{};
    unsigned revision = 0;
};

// One serialized worker owns all runtime and session-store access. Result publication
// is release/acquire; a renderer only sees complete snapshots on its own thread.
class App
{
  public:
    App() = default;
    ~App();
    App(const App &) = delete;
    App &operator=(const App &) = delete;
    bool initialize();
    void poll();
    void shutdown();
    const State &state() const
    {
        return state_;
    }
    bool busy() const
    {
        return running_;
    }
    bool generating() const;
    bool can_send() const;
    bool select_model(unsigned index);
    bool open_session(unsigned index);
    bool delete_session(unsigned index);
    bool new_session();
    bool send(const std::string &text);
    bool retry();
    bool play_audio();
    void set_preferences(Preferences preferences);

  private:
    enum class Job
    {
        Discover,
        Select,
        Open,
        Delete,
        Generate,
        Save,
        Preferences,
        Play
    };
    bool start(Job job);
    static void *worker(void *context);
    static void stream_callback(const char *text);
    void work();
    void generate();
    void refresh_sessions();
    void refresh_images();
    void join();
    State state_, result_;
    Job job_ = Job::Discover;
    unsigned argument_ = 0;
    std::string session_id_;
    bool running_ = false, preferences_dirty_ = false;
    std::atomic<bool> done_{false};
    std::atomic_flag stream_lock_ = ATOMIC_FLAG_INIT;
    std::atomic<unsigned> stream_version_{0};
    unsigned displayed_stream_ = 0;
    char stream_buffer_[4096]{};
#ifdef PROSPERO_HOST
    pthread_t thread_{};
#else
    void *thread_ = nullptr;
#endif
    static App *active_;
};
} // namespace prospero
