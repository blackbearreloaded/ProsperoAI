// ProsperoAI native OpenGL frontend.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_ui.hpp"
#include "dev_script.hpp"
#include "gpt_input.hpp"
#include "gpt_ime.hpp"
#include "audio/cues.hpp"
#include "gfx/canvas.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/system.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <vector>

void prospero_setup_sdl_memory();
extern "C" void ps5SetInterfaceThread() noexcept;
extern "C" int ps5_agc_backend_reserve_memory(void);

namespace
{
// A scripted run's pictures: half the canvas is enough to compare with the PC's.
constexpr int kPictureWidth = 960, kPictureHeight = 540;

// The bound framebuffer, bottom row first, as a 24-bit BMP.
bool save_picture(const std::string &path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const std::uint32_t row = (static_cast<std::uint32_t>(width) * 3 + 3) & ~3u;
    const std::uint32_t size = 54 + row * static_cast<std::uint32_t>(height);
    unsigned char header[54] = {'B', 'M'};
    const auto put = [&](int at, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, static_cast<std::uint32_t>(width));
    put(22, static_cast<std::uint32_t>(height));
    header[26] = 1;
    header[28] = 24;
    put(34, size - 54);
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return false;
    bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header);
    std::vector<unsigned char> line(row);
    for (int y = 0; y < height && ok; ++y)
    {
        const unsigned char *source = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
        {
            line[static_cast<std::size_t>(x) * 3] = source[x * 4 + 2];
            line[static_cast<std::size_t>(x) * 3 + 1] = source[x * 4 + 1];
            line[static_cast<std::size_t>(x) * 3 + 2] = source[x * 4];
        }
        ok = std::fwrite(line.data(), 1, line.size(), file) == line.size();
    }
    return std::fclose(file) == 0 && ok;
}

void ime_result(const char *value, void *context)
{
    static_cast<prospero::NativeUI *>(context)->keyboard_result(value);
}
void input_event(const gpt_input_event_t &event, hui::InputFrame &input, prospero::NativeUI &ui,
                 hui::ui::Feedback &feedback)
{
    if (!event.pressed)
        return;
    using hui::Action;
    using hui::Direction;
    constexpr Action actions[] = {Action::confirm,   Action::back, Action::west,
                                  Action::north,     Action::menu, Action::page_prev,
                                  Action::page_next, Action::up,   Action::down,
                                  Action::left,      Action::right};
    if (event.key <= GPT_INPUT_RIGHT)
    {
        input.pressed |= hui::action_bit(actions[event.key]);
        constexpr Direction directions[] = {Direction::up, Direction::down, Direction::left,
                                            Direction::right};
        if (event.key >= GPT_INPUT_UP)
            input.nav = directions[event.key - GPT_INPUT_UP];
    }
    else if (event.key == GPT_INPUT_SCROLL_UP)
        ui.scroll(-120);
    else if (event.key == GPT_INPUT_SCROLL_DOWN)
        ui.scroll(120);
    else if (event.key == GPT_INPUT_TEXT)
        ui.type(event.text, feedback);
    else if (event.key == GPT_INPUT_BACKSPACE)
        ui.backspace(feedback);
    else if (event.key == GPT_INPUT_ENTER)
        ui.submit(feedback);
}
} // namespace

int main()
{
    // This thread's large allocations stay out of the model runtimes' arena, and the
    // inference scratch takes its address range before the OpenGL runtime maps anything.
    ps5SetInterfaceThread();
    if (ps5_agc_backend_reserve_memory() != 0)
        hui::sys::log("[prosperoai] the inference scratch could not be reserved");
    prospero_setup_sdl_memory();
    SDL_Init(0); // Existing media decoder, clock and IME helpers; no SDL video.
    hui::ps5::Display display;
    if (!display.open(1920, 1080))
        hui::sys::quit();
    hui::gfx::Renderer renderer;
    if (!renderer.init())
        hui::sys::quit();
    prospero::FontSet fonts;
    if (!fonts.open(renderer, "/app0/assets/fonts"))
        hui::sys::quit();
    hui::audio::Mixer mixer;
    hui::audio::SoundBank sounds;
    const auto loaded = sounds.load("/app0/assets/audio/sfx");
    hui::sys::log("[prosperoai] interface sounds: %d loaded, %d rejected", loaded.files,
                  loaded.rejected);
    hui::ps5::AudioOut audio;
    if (!audio.start(mixer))
        hui::sys::log("[prosperoai] interface audio unavailable");
    gpt_input_init();
    gpt_ime_init();
    prospero::App app;
    prospero::NativeUI ui(app, fonts, renderer);
    ui.request_keyboard = [&](const std::string &value)
    { gpt_ime_request(value.c_str(), ime_result, &ui); };
    prospero::UiFrame frame;
    frame.glass_texture = renderer.glass_texture();
    hui::ui::Feedback feedback;
    // A test run written in the install folder's dev/request.txt; nothing without it.
    prospero::DevScript script;
    if (script.load("/app0/dev/request.txt", "/download0/ProsperoAI/dev"))
        hui::sys::log("[prosperoai] scripted run");
    hui::gfx::Canvas picture;
    auto previous = hui::sys::monotonic_us();
    bool first_frame = true;
    while (!ui.quit_requested() && !script.quit())
    {
        // Animation time runs from frame start to frame start, and is clamped.
        const auto now = hui::sys::monotonic_us();
        const float elapsed = static_cast<float>(now - previous) / 1000000.0f;
        const float dt = std::clamp(elapsed, 0.001f, 0.05f);
        previous = now;
        feedback.clear();
        hui::InputFrame input;
        gpt_input_poll();
        const bool intercepted = gpt_ime_active();
        gpt_input_event_t event;
        while (gpt_input_next(&event))
            if (!intercepted)
                input_event(event, input, ui, feedback);
        gpt_ime_poll();
        script.update(elapsed, app, ui, input, feedback);
        ui.update(input, dt, feedback);
        const float gain = static_cast<float>(app.state().preferences.volume) / 100;
        mixer.set_bus_gain(hui::audio::Bus::ui, gain);
        mixer.set_bus_gain(hui::audio::Bus::sfx, gain);
        for (const auto &cue : feedback.cues)
            sounds.play(mixer, hui::audio::SoundSet::glass, cue);
        frame.reset();
        ui.draw(frame);
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        if (frame.glass)
            renderer.glass();
        renderer.draw(frame.overlay);
        renderer.present(0, display.width(), display.height());
        if (!script.capture().empty())
        {
            // The same frame drawn once more into a small off-screen target: reading
            // the display surface back is slow.
            if (picture.texture() == 0)
                picture.create(kPictureWidth, kPictureHeight, 1);
            picture.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer.present(picture.framebuffer(), kPictureWidth, kPictureHeight);
            glBindFramebuffer(GL_FRAMEBUFFER, picture.framebuffer());
            script.capture_done(save_picture(script.capture(), kPictureWidth, kPictureHeight));
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            previous = hui::sys::monotonic_us(); // saving is slow; the frame was not
        }
        if (!display.swap())
            break;
        if (first_frame)
        {
            // The console's splash stays up until this first picture; the model
            // catalogue is read behind the opening that follows it.
            hui::sys::hide_splash_screen();
            app.initialize();
            first_frame = false;
        }
    }
    gpt_ime_shutdown();
    app.shutdown();
    gpt_input_shutdown();
    audio.stop();
    ui.release();
    renderer.release();
    display.close();
    SDL_Quit();
    hui::sys::quit();
}
