// ProsperoAI native OpenGL frontend.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_ui.hpp"
#include "gpt_input.hpp"
#include "gpt_ime.hpp"
#include "audio/cues.hpp"
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/audio_out.hpp"
#include "platform/ps5/system.hpp"
#include <SDL2/SDL.h>
#include <algorithm>

void prospero_setup_sdl_memory();

namespace
{
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
    auto previous = hui::sys::monotonic_us();
    bool first_frame = true;
    while (!ui.quit_requested())
    {
        // Animation time runs from frame start to frame start, and is clamped.
        const auto now = hui::sys::monotonic_us();
        const float dt = std::clamp(static_cast<float>(now - previous) / 1000000.0f, 0.001f, 0.05f);
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
