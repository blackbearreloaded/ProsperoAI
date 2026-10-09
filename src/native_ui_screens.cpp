// ProsperoAI native frontend: what each page draws.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_ui_impl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prospero
{
namespace
{
constexpr const char *kKinds[] = {"CONVERSATION", "IMAGE", "SOUND", "VOICE"};
constexpr const char *kKindNames[] = {"Converse", "Imagine", "Compose", "Speak"};
constexpr const char *kKindTags[] = {"01 / TEXT", "02 / IMAGE", "03 / SOUND", "04 / VOICE"};
constexpr const char *kKindLines[] = {"Conversation model", "Image model", "Sound model",
                                      "Voice model"};

// Draws one part of a page as it arrives: faded and a few pixels low until it settles.
template <typename Draw> void part(gfx::DrawList &list, float arrival, Draw &&draw)
{
    list.push_opacity(arrival);
    list.push_transform(1, 0, 0, 0, (1 - arrival) * 16);
    draw();
    list.pop_transform();
    list.pop_opacity();
}
} // namespace

void NativeUI::Impl::draw_model_art(gfx::DrawList &list, int kind, float cx, float cy, float size,
                                    Color color, float phase, float focus) const
{
    if (kind == 0)
        draw_sculpture(list, cx, cy, size, color, clock_ + phase, size > 100 ? 30 : 18);
    else if (kind == 1)
    {
        // Sheets of an image coming into register.
        const float unit = size / 78;
        const float drift = reduced_motion() ? 0 : 0.05f * std::sin(clock_ * 0.6f + phase);
        for (int i = 0; i < 7; ++i)
        {
            const float f = static_cast<float>(i);
            list.rotated_rect({cx - (48 - f * 3) * unit, cy - (48 - f * 2) * unit,
                               (92 - f * 5) * unit, (92 - f * 5) * unit},
                              12 * unit, -0.4f + f * 0.13f + drift * (7 - f),
                              color.with_alpha(0.14f + f * 0.05f));
        }
    }
    else if (kind == 2)
    {
        // A waveform; it breathes a little when it has the focus.
        const float unit = size / 78;
        for (int i = 0; i < 25; ++i)
        {
            const float x = static_cast<float>(i - 12);
            const float live =
                reduced_motion() ? 0 : focus * 0.25f * std::sin(clock_ * 3.2f + x * 0.9f);
            const float h =
                (12 + 52 * std::exp(-x * x / 68) *
                          (0.48f + 0.52f * std::abs(std::sin(x * 0.7f + phase)) + live)) *
                unit;
            list.rounded_rect({cx + x * 5 * unit - 1.5f * unit, cy - h, 3 * unit, h * 2},
                              1.5f * unit, color.with_alpha(0.6f + 0.4f * focus));
        }
    }
    else
    {
        // A voice: a point and the arcs leaving it.
        const float unit = size / 78;
        list.circle(cx - 34 * unit, cy, 9 * unit, color);
        for (int i = 0; i < 4; ++i)
        {
            const float f = static_cast<float>(i);
            const float pulse =
                reduced_motion() ? 0.5f : 0.5f + 0.5f * std::sin(clock_ * 2.4f - f * 0.8f + phase);
            list.arc(cx - 34 * unit, cy, (30 + f * 18) * unit, 3 * unit, 0.5f, 2.14f,
                     color.with_alpha((0.85f - f * 0.17f) * (0.55f + 0.45f * pulse)));
        }
    }
}

void NativeUI::Impl::draw_welcome(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &state = app_.state();
    part(list, arrival(1), [&]
         { label(list, "A LITTLE CURIOSITY. ENDLESS POSSIBILITY.", 120, 254, palette_.accent); });
    part(list, arrival(2),
         [&]
         {
             ui::text(list, fonts_.display, "Big ideas.", 114, 382, 96, palette_.ink);
             ui::text(list, fonts_.display, "Right here.", 114, 496, 96, palette_.accent);
         });
    part(list, arrival(3),
         [&]
         {
             text(list, "A space to think, imagine, and create.", 120, 560, 29, palette_.muted);
             text(list, "Powered by your PS5. Entirely yours.", 120, 604, 29, palette_.muted);
         });
    part(list, arrival(4),
         [&]
         {
             const Rect button{120, 652, 372, 74};
             const bool ready = app_.can_send();
             const char *action = ready                  ? "Start a conversation"
                                  : app_.busy()          ? "Getting ready..."
                                  : state.models.empty() ? "Add a model to begin"
                                                         : "Choose a model";
             if (ready)
                 list.glow(button, 37, 30,
                           palette_.accent.with_alpha(0.10f + 0.12f * ui::breathe(clock_, 2.8f)));
             ui::Painter paint(list, fonts_, theme_);
             paint.button(button, action, ui::ButtonKind::primary, {1, 0, !ready && app_.busy()});
             float x = 520;
             if (app_.busy())
             {
                 spinner(list, x + 12, 690, 11, palette_.accent);
                 x += 38;
             }
             text(list, fit(state.status, 23, 640, false), x, 698, 23, palette_.muted);
         });
    // The sculpture: the app's one piece of art, drawn from lines.
    const float grown = arrival(2);
    list.glow({1240, 330, 300, 220}, 110, 150, palette_.accent.with_alpha(0.07f * grown));
    draw_sculpture(list, 1390, 440, 250 + 60 * grown, palette_.accent.with_alpha(grown), clock_,
                   48);
    part(list, arrival(4),
         [&] { label(list, "THOUGHT, TAKING SHAPE", 1238, 742, palette_.muted); });

    // What is installed, by what it can do.
    for (int i = 0; i < 4; ++i)
    {
        part(list, arrival(5 + i),
             [&]
             {
                 const Rect tile{kLeft + static_cast<float>(i) * 438, 800, 414, 134};
                 const Color kind = palette_.kinds[i];
                 const unsigned count = state.model_counts[static_cast<std::size_t>(i)];
                 const bool current =
                     state.selected_model >= 0 &&
                     static_cast<int>(
                         state.models[static_cast<std::size_t>(state.selected_model)].capability) ==
                         i;
                 panel(list, tile, 22, current ? 0.55f : 0.0f);
                 list.push_clip(tile.inset(1));
                 draw_model_art(list, i, tile.x + tile.w - 78, tile.cy(), 44,
                                kind.with_alpha(count ? 0.9f : 0.35f), static_cast<float>(i) * 9,
                                0);
                 list.pop_clip();
                 label(list, kKindTags[i], tile.x + 26, tile.y + 40, count ? kind : palette_.muted);
                 text(list, kKindNames[i], tile.x + 26, tile.y + 82, 30, palette_.ink, true);
                 char installed[48];
                 if (!state.initialized)
                     std::snprintf(installed, sizeof(installed), "Looking...");
                 else if (count == 0)
                     std::snprintf(installed, sizeof(installed), "No model yet");
                 else
                     std::snprintf(installed, sizeof(installed), "%u model%s installed", count,
                                   count == 1 ? "" : "s");
                 text(list, installed, tile.x + 26, tile.y + 113, 21, palette_.muted);
             });
    }
}

void NativeUI::Impl::draw_conversation(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &state = app_.state();
    const bool generating = app_.generating();

    part(list, arrival(1),
         [&]
         {
             panel(list, {kLeft, 196, 362, 740}, 24, rail_ ? 0.35f : 0.0f);
             label(list, "YOUR SPACE", 122, 246, palette_.muted);
             text(list, "Conversations", 122, 292, 31, palette_.ink, true);
             sessions_.draw(canvas);
         });

    const char *name = state.session.id[0] ? state.session.model_name : current_name();
    const Capability capability =
        state.selected_model >= 0
            ? state.models[static_cast<std::size_t>(state.selected_model)].capability
            : Capability::Text;
    part(list, arrival(2),
         [&]
         {
             label(list, "WORKSPACE", 536, 232, palette_.accent);
             text(list,
                  fit(state.session.id[0] ? state.session.title : "A new conversation", 46, 1280),
                  536, 290, 46, palette_.ink, true);
             float x = 536;
             x += chip(list, x, 332, fit(name, 19, 420), kind_color(capability), true) + 12;
             if (state.session.id[0] && !app_.can_send() && !app_.busy())
                 chip(list, x, 332, "Read only", palette_.warn, false);
         });

    const float line = body_size_ * 1.48f;
    const float width = chat_.bounds().w;
    chat_.begin(canvas);
    for (std::size_t index = 0; index < chat_rows_.size(); ++index)
    {
        const auto &row = chat_rows_[index];
        if (row.y + row.height < chat_.offset_y() || row.y > chat_.offset_y() + chat_.bounds().h)
            continue;
        const float appear =
            reduced_motion() ? 1 : tween::cubic_out(std::min(row.age / 0.32f, 1.0f));
        list.push_opacity(appear);
        list.push_transform(1, 0, 0, 0, (1 - appear) * 14);
        const bool user = row.role == "user";
        const bool live = generating && index + 1 == chat_rows_.size() && !user;
        float y = 0;
        if (user)
        {
            const float bubble = std::min(row.width + 56, 956.0f);
            const Rect r{width - bubble, row.y, bubble, row.height - 28};
            list.bordered_rect(r, 22, gfx::mix(palette_.raised, palette_.accent, 0.12f), 1,
                               palette_.accent.with_alpha(0.28f));
            y = row.y + 17 + body_size_;
            for (const auto &text_line : row.lines)
            {
                text(list, text_line, r.x + 28, y, body_size_, palette_.ink);
                y += line;
            }
        }
        else
        {
            draw_mark(list, 16, row.y + 22, 0.75f, kind_color(capability));
            text(list, fit(name, 22, 600), 44, row.y + 30, 22, palette_.ink, true);
            y = row.y + 58 + body_size_ * 0.75f;
            if (live && row.text.empty())
                thinking_dots(list, 2, row.y + 78, palette_.accent);
            float caret_x = 0;
            for (const auto &text_line : row.lines)
            {
                caret_x = text(list, text_line, 0, y, body_size_, palette_.ink.with_alpha(0.92f));
                y += line;
            }
            if (live && !row.text.empty() && (reduced_motion() || std::fmod(clock_, 1.0f) < 0.6f))
                list.rounded_rect({caret_x + 6, y - line - body_size_ * 0.78f, 3, body_size_}, 1.5f,
                                  palette_.accent);
            y += 8 - body_size_ * 0.75f;
            if (!row.image_path.empty())
            {
                const auto texture = images_.find(row.image_path);
                if (texture != images_.end())
                {
                    const Rect picture{0, y, std::min(1240.0f, 360 * texture->second.aspect),
                                       std::min(360.0f, 1240 / texture->second.aspect)};
                    list.shadow({picture.x, picture.y + 12, picture.w, picture.h}, 18, 36,
                                Color::rgb(0x000000, 0.35f));
                    list.image(texture->second.id, picture, {0, 0, 1, 1}, Color::rgb(0xffffff), 18);
                }
                else
                {
                    const bool decoded =
                        std::any_of(state.images.begin(), state.images.end(),
                                    [&](const auto &i) { return i->path == row.image_path; });
                    const Rect frame{0, y, 640, 360};
                    list.bordered_rect(frame, 18, palette_.panel, 1, palette_.line);
                    if (decoded)
                        spinner(list, frame.cx(), frame.cy(), 16, palette_.accent);
                    else
                        text(list, "Image preview unavailable", frame.cx(), frame.cy() + 8, 24,
                             palette_.muted, false, gfx::Align::center);
                }
            }
            else if (row.audio)
            {
                const Rect card{0, y, 640, 92};
                panel(list, card, 20);
                const auto glyphs = palette_.day ? ui::GlyphStyle::light() : ui::GlyphStyle::dark();
                ui::draw_button(list, fonts_, glyphs, ui::Button::triangle, card.x + 26, card.cy(),
                                34);
                text(list, "Play saved audio", card.x + 78, card.cy() + 9, 26, palette_.ink, true);
                list.push_clip(card.inset(1));
                draw_model_art(list, 2, card.x + card.w - 110, card.cy(), 34,
                               kind_color(capability), static_cast<float>(index), 0);
                list.pop_clip();
            }
        }
        list.pop_transform();
        list.pop_opacity();
    }
    if (chat_rows_.empty())
    {
        const float calm = arrival(3);
        list.push_opacity(calm);
        draw_mark(list, 30, 62, 1.6f, palette_.accent);
        text(list, "What would you like to explore?", 0, 150, 40, palette_.ink, true);
        text(list, "Ask, imagine, or describe. Everything stays on this console.", 0, 198, 26,
             palette_.muted);
        list.pop_opacity();
    }
    chat_.end(canvas);
    chat_.draw(canvas);

    part(list, arrival(3),
         [&]
         {
             // The line above the composer: what is happening, and what the last answer took.
             float x = 536;
             if (generating)
             {
                 spinner(list, x + 11, 813, 10, palette_.accent);
                 x += 34;
                 char elapsed[64];
                 std::snprintf(elapsed, sizeof(elapsed),
                               "Creating on your console  \xC2\xB7  %.0f s",
                               static_cast<double>(busy_seconds_));
                 text(list, elapsed, x, 820, 21, palette_.muted);
             }
             else
             {
                 if (app_.busy())
                 {
                     spinner(list, x + 11, 813, 10, palette_.accent);
                     x += 34;
                 }
                 text(list, fit(state.status, 21, 760, false), x, 820, 21,
                      state.retry_available ? palette_.warn : palette_.muted);
                 const std::string stats = stats_line();
                 if (!stats.empty())
                     ui::text(list, fonts_.mono, stats, kRight, 820, 19, palette_.muted,
                              gfx::Align::right);
             }
             if (send_pulse_.value > 0.01f)
                 list.glow(composer_.bounds(), 16, 30,
                           palette_.accent.with_alpha(0.35f * send_pulse_.value));
             composer_.draw(canvas);
         });
}

void NativeUI::Impl::draw_models(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &state = app_.state();
    if (filters_.active() == 5)
    {
        label(list, "HUGGING FACE", kLeft, 222, palette_.accent);
        text(list, "Bring a model home.", 92, 300, 62, palette_.ink, true);
        text(list, "Enter a public repository as owner/name, then choose a verified GGUF file.",
             kLeft, 346, 25, palette_.muted);
        search_.draw(canvas);
        text(list, "Enter repository  ·  Square: edit  ·  Cross: browse or download", kLeft, 482,
             22, palette_.muted);
        char status[192]{};
        prospero_model_download::status(status, sizeof(status));
        const auto downloader_state = prospero_model_download::state();
        if (downloader_state == prospero_model_download::State::Loading ||
            downloader_state == prospero_model_download::State::Downloading)
            spinner(list, kLeft + 12, 548, 9, palette_.accent);
        text(list, status, kLeft + 32, 556, 23,
             downloader_state == prospero_model_download::State::Failed ? palette_.bad : palette_.muted);
        if (downloader_state == prospero_model_download::State::Ready)
        {
            for (std::size_t i = 0; i < prospero_model_download::candidate_count() && i < 8; ++i)
            {
                prospero_model_download::Candidate candidate{};
                if (!prospero_model_download::candidate(i, &candidate)) continue;
                const float y = 612.0f + static_cast<float>(i) * 42.0f;
                const Color ink = static_cast<int>(i) == download_focus_ ? palette_.accent : palette_.ink;
                if (static_cast<int>(i) == download_focus_) list.circle(kLeft + 6, y - 7, 4, ink);
                char row[220];
                std::snprintf(row, sizeof(row), "%s  ·  %llu MiB", candidate.name,
                              static_cast<unsigned long long>(candidate.size / (1024 * 1024)));
                text(list, fit(row, 22, 1640, false), kLeft + 24, y, 22, ink);
            }
        }
        return;
    }
    if (state.models.empty())
    {
        // Nothing to browse: say what to do about it.
        part(list, arrival(1),
             [&]
             {
                 const Rect card{460, 300, 1000, 440};
                 panel(list, card, 28);
                 draw_sculpture(list, card.cx(), card.y + 130, 84, palette_.accent, clock_, 24);
                 text(list, state.initialized ? "No models installed yet" : "Looking for models...",
                      card.cx(), card.y + 262, 40, palette_.ink, true, gfx::Align::center);
                 if (state.initialized)
                 {
                     text(list, "Copy your models into the shared model folder on this console,",
                          card.cx(), card.y + 316, 25, palette_.muted, false, gfx::Align::center);
                     text(list, "then open ProsperoAI again.",
                          card.cx(), card.y + 352, 25, palette_.muted, false, gfx::Align::center);
                     ui::text(list, fonts_.mono, "/data/homebrew/prosperoai/models", card.cx(),
                              card.y + 404, 22, palette_.accent, gfx::Align::center);
                 }
             });
        return;
    }

    // The model under the focus leads the page.
    if (hero_model_ >= 0)
    {
        const auto &model = state.models[static_cast<std::size_t>(hero_model_)];
        const int kind = static_cast<int>(model.capability);
        const Color colour = palette_.kinds[kind];
        const float fade =
            reduced_motion() ? 1 : tween::cubic_out(std::min(hero_age_ / 0.24f, 1.0f));
        list.push_opacity(fade);
        list.push_transform(1, 0, 0, (1 - fade) * 18, 0);
        label(list, kKinds[kind], kLeft, 222, colour);
        text(list, fit(model.name, 62, 1180), 92, 300, 62, palette_.ink, true);
        text(list, fit(std::string(kKindLines[kind]) + "  \xC2\xB7  " + model.id, 25, 1180, false),
             kLeft, 346, 25, palette_.muted);
        list.glow({1560, 216, 180, 110}, 60, 90, colour.with_alpha(0.07f));
        draw_model_art(list, kind, 1650, 272, 84, colour, static_cast<float>(hero_model_) * 12, 1);
        list.pop_transform();
        list.pop_opacity();
        if (hero_model_ == pending_model_ && app_.busy())
            chip(list, 1500, 222, "Preparing", palette_.warn, true, true);
        else if (hero_model_ == active_model_)
            chip(list, 1500, 222, state.ready ? "Active" : "Needs attention",
                 state.ready ? palette_.good : palette_.bad, true, true);
    }
    else
    {
        label(list, "YOUR MODELS", kLeft, 222, palette_.accent);
        text(list, "Nothing matches.", 92, 300, 62, palette_.ink, true);
        text(list, "Try another name, or another kind of model.", kLeft, 346, 25, palette_.muted);
    }

    part(list, arrival(1),
         [&]
         {
             search_.draw(canvas);
             filters_.draw(canvas);
         });
    part(list, arrival(2), [&] { models_.draw(canvas); });
    char count[96];
    std::snprintf(count, sizeof(count), "%zu shown  /  %zu installed", visible_models_.size(),
                  state.models.size());
    text(list, count, kLeft, 948, 21, palette_.muted);
    text(list, "A conversation remembers its model.", kRight, 948, 21, palette_.muted, false,
         gfx::Align::right);
}

void NativeUI::Impl::draw_model(gfx::DrawList &list, const Rect &r, int index, float focus) const
{
    const auto &state = app_.state();
    const auto &model = state.models[static_cast<std::size_t>(index)];
    const int kind = static_cast<int>(model.capability);
    const Color colour = palette_.kinds[kind];
    if (focus > 0.01f)
        list.glow(r, 22, 34, colour.with_alpha(0.16f * focus));
    list.bordered_rect(r, 22, gfx::mix(palette_.panel, palette_.raised, focus * 0.55f), 1,
                       gfx::mix(palette_.line, colour.with_alpha(0.55f), focus));
    list.push_clip(r.inset(1));
    list.gradient_rect_h({r.x + 1, r.y + 1, r.w * 0.55f, r.h - 2}, 22,
                         colour.with_alpha(0.10f + 0.06f * focus), colour.with_alpha(0.0f));
    draw_model_art(list, kind, r.x + 97, r.cy(), 78, colour, static_cast<float>(index) * 12, focus);
    label(list, kKinds[kind], r.x + 196, r.y + 55, colour);
    text(list, fit(model.name, 30, r.w - 224), r.x + 196, r.y + 97, 30, palette_.ink, true);
    text(list, fit(model.id, 20, r.w - 224, false), r.x + 196, r.y + 131, 20, palette_.muted);
    if (index == pending_model_ && app_.busy())
    {
        spinner(list, r.x + 206, r.y + 163, 8, palette_.warn);
        text(list, "Preparing...", r.x + 224, r.y + 170, 20, palette_.warn);
    }
    else if (index == active_model_)
    {
        const Color tone = state.ready ? palette_.good : palette_.bad;
        list.circle(r.x + 202, r.y + 163, 4, tone);
        text(list, state.ready ? "Active" : "Needs attention", r.x + 216, r.y + 170, 20, tone);
    }
    list.pop_clip();
}

void NativeUI::Impl::draw_settings(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &state = app_.state();
    part(list, arrival(1),
         [&]
         {
             label(list, "SETTINGS", kLeft, 222, palette_.accent);
             text(list, "Make it feel like yours.", 92, 296, 58, palette_.ink, true);
         });
    part(list, arrival(2),
         [&]
         {
             panel(list, {kLeft, 338, 350, 568}, 24, category_focus_ ? 0.35f : 0.0f);
             categories_.draw(canvas);
         });
    part(list, arrival(3),
         [&]
         {
             panel(list, {470, 338, 960, 568}, 24, category_focus_ ? 0.0f : 0.2f);
             form_.draw(canvas);
         });
    part(list, arrival(4),
         [&]
         {
             const Rect card{1454, 338, 370, 568};
             panel(list, card, 24);
             list.push_clip(card.inset(1));
             if (category_ == 4)
             {
                 // About: the mark, the name and where it runs.
                 draw_sculpture(list, card.cx(), card.y + 150, 92, palette_.accent, clock_, 24);
                 ui::text(list, fonts_.display, "ProsperoAI", card.cx(), card.y + 318, 38,
                          palette_.ink, gfx::Align::center);
                 label(list, "THOUGHT, TAKING SHAPE", card.cx(), card.y + 356, palette_.accent,
                       gfx::Align::center);
                 ui::paragraph(list, fonts_.regular,
                               "Models run on this console's own hardware. No account, no cloud, "
                               "nothing uploaded.",
                               card.x + 30, card.y + 420, 22, card.w - 60, 32, palette_.muted, 4);
             }
             else
             {
                 // A small conversation in the colours and the reading size being chosen.
                 label(list, "LIVE PREVIEW", card.x + 30, card.y + 46, palette_.accent);
                 const float size = state.preferences.text_size ? 27.0f : 23.0f;
                 const Rect bubble{card.x + 90, card.y + 80, card.w - 120, size + 38};
                 list.bordered_rect(bubble, 18, gfx::mix(palette_.raised, palette_.accent, 0.12f),
                                    1, palette_.accent.with_alpha(0.28f));
                 text(list, "Tell me a story.", bubble.x + 22, bubble.cy() + size * 0.35f, size,
                      palette_.ink);
                 draw_mark(list, card.x + 44, card.y + 190, 0.7f, palette_.accent);
                 text(list, "ProsperoAI", card.x + 70, card.y + 197, 20, palette_.ink, true);
                 const float after = ui::paragraph(
                     list, fonts_.regular, "Once, a small light learned to think for itself.",
                     card.x + 30, card.y + 244, size, card.w - 60, size * 1.45f,
                     palette_.ink.with_alpha(0.92f), 4);
                 const Rect send{card.x + 30, std::max(after + 10, card.y + 400), card.w - 60, 58};
                 ui::Painter paint(list, fonts_, theme_);
                 paint.button(send, "Send", ui::ButtonKind::primary, {0, 0, false});
                 draw_model_art(list, 2, card.cx(), card.y + 512, 36, palette_.accent, 3, 1);
             }
             list.pop_clip();
         });
}
} // namespace prospero
