// Deterministic host fixtures for rendering the production frontend with Mesa.
// No model weights, console connection, or deployment is involved.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_ui.hpp"
#include "gfx/renderer.hpp"
#include "gfx/gl_program.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <thread>

namespace
{
unsigned selected = 0;
const char *names[] = {"Mistral 7B", "Qwen3.5 9B", "SD-Turbo", "Stable Audio", "Kokoro"};
const char *ids[] = {"mistral-7b", "qwen35-9b", "sd-turbo", "stable-audio", "kokoro"};
const char *purposes[] = {"text-to-text", "text-to-text", "text-to-image", "text-to-audio",
                          "text-to-speech"};
struct Saved
{
    prospero_session::Record record;
    std::vector<prospero_session::Message> messages;
};
std::map<std::string, Saved> saved;
unsigned serial = 0;
std::string image_fixture;
std::string read(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
} // namespace
extern "C" std::FILE *__real_fopen(const char *, const char *);
extern "C" std::FILE *__wrap_fopen(const char *path, const char *mode)
{
    // The visual fixture supplies the console's generated image inside build/.
    if (std::strncmp(path, "/download0/", 11) == 0 && std::strstr(path, ".tga"))
        return __real_fopen(image_fixture.c_str(), mode);
    return __real_fopen(path, mode);
}
unsigned gpt_runtime_model_count()
{
    return std::getenv("PROSPERO_STRESS") ? 500 : 5;
}
unsigned gpt_runtime_selected_model()
{
    return selected;
}
const char *gpt_runtime_model_id(unsigned index)
{
    if (index < 5)
        return ids[index];
    static char id[48];
    std::snprintf(id, sizeof(id), "local-model-%03u", index);
    return id;
}
const char *gpt_runtime_model_name(unsigned index)
{
    return index < 5 ? names[index] : gpt_runtime_model_id(index);
}
const char *gpt_runtime_model_purpose(unsigned index)
{
    return purposes[index % 5];
}
bool gpt_runtime_select_model(unsigned index)
{
    selected = index;
    return true;
}
int gpt_runtime_prepare()
{
    return 0;
}
bool gpt_runtime_context_full()
{
    return false;
}
int gpt_runtime_generate(const gpt_runtime_message_t *, unsigned, const gpt_runtime_settings_t &,
                         char *output, std::size_t capacity, gpt_runtime_stats_t *stats,
                         gpt_runtime_progress_fn progress)
{
    if (selected == 2 || selected == 3)
    {
        std::snprintf(output, capacity, "%s",
                      selected == 2 ? "/download0/prosperoai-image-0.tga"
                                    : "/download0/prosperoai-audio.wav");
        return 0;
    }
    const char *reply =
        "Start with the things that make us human.\n\nA garden under warm light. A table big "
        "enough for everyone.\nA window that frames Earth, so home never feels too far "
        "away.\n\nThe technology keeps you alive. The small rituals make you belong.";
    std::string stream;
    for (const char *at = reply; *at; ++at)
    {
        stream += *at;
        progress(stream.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::snprintf(output, capacity, "%s", reply);
    stats->generated_tokens = 64;
    return 0;
}
namespace prospero_session
{
bool create(Record *r, const char *id, const char *name, const char *purpose)
{
    std::snprintf(r->id, sizeof(r->id), "preview-%u", ++serial);
    std::snprintf(r->model_id, sizeof(r->model_id), "%s", id);
    std::snprintf(r->model_name, sizeof(r->model_name), "%s", name);
    std::snprintf(r->purpose, sizeof(r->purpose), "%s", purpose);
    return true;
}
bool save(Record *r, const Message *messages, unsigned count)
{
    r->message_count = count;
    saved[r->id] = {*r, {messages, messages + count}};
    return true;
}
bool load(const char *id, Record *record, Message *messages, unsigned capacity)
{
    const auto it = saved.find(id);
    if (it == saved.end() || it->second.messages.size() > capacity)
        return false;
    *record = it->second.record;
    std::copy(it->second.messages.begin(), it->second.messages.end(), messages);
    return true;
}
unsigned scan(Record *records, unsigned capacity)
{
    unsigned count = 0;
    for (const auto &[id, entry] : saved)
        if (count < capacity)
            records[count++] = entry.record;
    return count;
}
bool erase(const char *id)
{
    return saved.erase(id) != 0;
}
bool archive_media(const char *id, unsigned index, const char *kind, const char *, char *path,
                   std::size_t capacity)
{
    std::snprintf(path, capacity, "/download0/ProsperoAI/sessions/%s/media/%s-%u.%s", id, kind,
                  index, std::strcmp(kind, "image") == 0 ? "tga" : "wav");
    return true;
}
} // namespace prospero_session

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    const std::string assets = argv[1], fallback_path = argv[2], output = argv[3];
    const bool record = std::getenv("PROSPERO_REEL") != nullptr;
    const int width = record ? 1280 : 1920, height = record ? 720 : 1080;
    image_fixture = output + "/image-fixture.tga";
    {
        std::ofstream file(image_fixture, std::ios::binary);
        unsigned char header[18]{};
        header[2] = 2;
        header[13] = 1;
        header[14] = 128;
        header[16] = 24;
        header[17] = 0x20;
        file.write(reinterpret_cast<char *>(header), sizeof(header));
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 256; ++x)
            {
                unsigned char pixel[] = {static_cast<unsigned char>(80 + x / 2),
                                         static_cast<unsigned char>(40 + y),
                                         static_cast<unsigned char>(190 - x / 3)};
                file.write(reinterpret_cast<char *>(pixel), 3);
            }
    }
    auto platform = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display = platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    EGLint major = 0, minor = 0;
    assert(eglInitialize(display, &major, &minor) && eglBindAPI(EGL_OPENGL_API));
    const EGLint attributes[] = {EGL_CONTEXT_MAJOR_VERSION,
                                 4,
                                 EGL_CONTEXT_MINOR_VERSION,
                                 5,
                                 EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                 EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                 EGL_NONE};
    EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    assert(context != EGL_NO_CONTEXT &&
           eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
    hui::gfx::set_glsl_prefix("#version 450 core\n");
    hui::gfx::Renderer renderer;
    assert(renderer.init());
    hui::gfx::Font regular, semibold, title, mono, fallback;
    hui::ui::Fonts fonts;
    auto font = [&](const char *name, hui::gfx::Font &face, hui::ui::FontRef &ref)
    {
        assert(face.load(read(assets + "/fonts/" + name)));
        ref = {&face, renderer.batch().create_font_texture(face)};
    };
    font("inter-regular.huifont", regular, fonts.regular);
    font("inter-semibold.huifont", semibold, fonts.semibold);
    font("montserrat-medium.huifont", title, fonts.display);
    font("dejavu-sans-mono.huifont", mono, fonts.mono);
    assert(fallback.load(read(fallback_path)));
    const auto fallback_texture = renderer.batch().create_font_texture(fallback);
    for (auto *face : {&regular, &semibold, &title, &mono})
        face->set_fallback(&fallback, fallback_texture);
    fonts.pixel = fonts.hand = fonts.regular;
    GLuint target = 0, storage = 0;
    glGenFramebuffers(1, &target);
    glGenRenderbuffers(1, &storage);
    glBindRenderbuffer(GL_RENDERBUFFER, storage);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, storage);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    std::remove(PROSPERO_SETTINGS_PATH);
    prospero::App app;
    prospero::NativeUI ui(app, fonts, renderer);
    assert(app.initialize());
    hui::ui::Feedback feedback;
    prospero::UiFrame frame;
    frame.glass_texture = renderer.glass_texture();
    auto render = [&]
    {
        frame.reset();
        ui.draw(frame);
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        if (frame.glass)
            renderer.glass();
        renderer.draw(frame.overlay);
        renderer.present(target, width, height);
        assert(glGetError() == GL_NO_ERROR);
    };
    unsigned frame_number = 0;
    std::vector<unsigned char> recording_pixels(static_cast<std::size_t>(width) * height * 3);
    auto update = [&](hui::InputFrame input = {})
    {
        feedback.clear();
        ui.update(input, 1.0f / 60, feedback);
        if (record && ++frame_number % 3 == 0)
        {
            render();
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, recording_pixels.data());
            assert(std::fwrite(recording_pixels.data(), 1, recording_pixels.size(), stdout) ==
                   recording_pixels.size());
        }
    };
    auto settle = [&]
    {
        for (int i = 0; i < 2000; ++i)
        {
            update();
            if (!app.busy())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(!app.busy());
        for (int i = 0; i < 60; ++i)
            update();
    };
    auto press = [&](hui::Action action, hui::Direction direction = hui::Direction::none)
    {
        hui::InputFrame input;
        input.pressed = hui::action_bit(action);
        input.nav = direction;
        update(input);
        for (int i = 0; i < 40; ++i)
            update();
    };
    auto capture = [&](const char *name)
    {
        render();
        const auto error = glGetError();
        std::fprintf(stderr, "%s: %zu calls, %zu instances, GL 0x%x\n", name,
                     renderer.last_draw_calls(), renderer.last_instances(), error);
        assert(error == GL_NO_ERROR);
        if (record)
            return;
        std::vector<unsigned char> rgb(1920 * 1080 * 3);
        glReadPixels(0, 0, 1920, 1080, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        std::FILE *file = std::fopen((output + "/" + name + ".ppm").c_str(), "wb");
        assert(file);
        std::fprintf(file, "P6\n1920 1080\n255\n");
        for (int y = 1079; y >= 0; --y)
            std::fwrite(rgb.data() + y * 1920 * 3, 1, 1920 * 3, file);
        assert(std::fclose(file) == 0);
    };
    settle();
    capture("welcome");
    press(hui::Action::page_next);
    capture("models");
    press(hui::Action::confirm);
    capture("switch-model");
    press(hui::Action::back);
    press(hui::Action::page_next);
    capture("settings");
    press(hui::Action::right, hui::Direction::right);
    settle();
    capture("daylight");
    press(hui::Action::left, hui::Direction::left);
    settle();
    press(hui::Action::page_prev);
    press(hui::Action::page_prev);
    assert(app.send("Imagine a quiet lunar outpost. What makes it feel like home?"));
    settle();
    ui.scroll(-10000);
    settle();
    capture("conversation");
    assert(app.send("你好。こんにちは。Привет. Tell me about the stars."));
    settle();
    capture("multilingual");
    assert(app.select_model(2));
    settle();
    assert(app.send("An image preview fixture"));
    settle();
    assert(app.state().images.size() == 1);
    ui.scroll(-10000);
    settle();
    capture("image");
    assert(app.select_model(3));
    settle();
    assert(app.send("A soft atmosphere for reading"));
    settle();
    capture("audio");
    if (!record)
    {
        press(hui::Action::page_next);
        auto preferences = app.state().preferences;
        preferences.reduced_motion = true;
        app.set_preferences(preferences);
        settle();
        render();
        std::vector<unsigned char> before(static_cast<std::size_t>(width) * height * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, before.data());
        for (int i = 0; i < 120; ++i)
            update();
        render();
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, recording_pixels.data());
        assert(before == recording_pixels);
        std::fprintf(stderr, "Reduced motion: identical frames after two seconds at rest.\n");
    }
    std::remove(image_fixture.c_str());
    app.shutdown();
    std::remove(PROSPERO_SETTINGS_PATH);
}
