// The production native frontend, shared by the console and host visual checks.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "font_set.hpp"
#include "native_app.hpp"
#include "core/input.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/renderer.hpp"
#include "ui/feedback.hpp"
#include "ui/fonts.hpp"
#include <functional>
#include <memory>

namespace prospero
{
// One frame, in the order the renderer takes it: the backdrop shader, the
// scene, and what floats above it. `glass` asks for the scene to be blurred
// into glass_texture before the overlay is drawn (dialogs are frosted).
struct UiFrame
{
    hui::gfx::BackdropSpec backdrop;
    hui::gfx::DrawList scene, overlay;
    bool glass = false;
    std::uint32_t glass_texture = 0;
    void reset()
    {
        backdrop = {};
        scene.clear();
        overlay.clear();
        glass = false;
    }
};

class NativeUI
{
  public:
    NativeUI(App &app, FontSet &fonts, hui::gfx::Renderer &renderer);
    ~NativeUI();
    void update(const hui::InputFrame &input, float dt, hui::ui::Feedback &feedback);
    void draw(UiFrame &frame) const;
    void type(char character, hui::ui::Feedback &feedback);
    void backspace(hui::ui::Feedback &feedback);
    void submit(hui::ui::Feedback &feedback);
    void scroll(float pixels);
    void keyboard_result(const char *text);
    // Native OS keyboard service. The host can supply an equivalent editor.
    std::function<void(const std::string &)> request_keyboard;
    bool quit_requested() const;
    // What the update shows: none, offer, notes, working, closing or failed.
    const char *update_state() const;
    void release();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace prospero
