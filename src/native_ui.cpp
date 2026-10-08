// ProsperoAI native frontend, built with ps5-homebrew-ui.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "native_ui.hpp"
#include "ui/components/scroll_area.hpp"
#include "ui/components/text_field.hpp"
#include "ui/components/dialog.hpp"
#include "ui/components/form.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/keyboard.hpp"
#include "ui/components/list.hpp"
#include "ui/components/search_field.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>

namespace prospero
{
using namespace hui;
} // namespace prospero

namespace prospero
{
namespace
{
using gfx::Color;
using gfx::Rect;
constexpr float kPi = 3.14159265f;
const Color kBackground = Color::rgb(0x0c0e12);
const Color kPanel = Color::rgb(0x15191f);
const Color kRaised = Color::rgb(0x20252c);
const Color kInk = Color::rgb(0xf4f1ea);
const Color kMuted = Color::rgb(0x9ca4b0);
const Color kLine = Color::rgb(0xffffff, 0.09f);
const Color kAmber = Color::rgb(0xf0be7d);
const Color kGreen = Color::rgb(0xa2c7ad);
const Color kClear = Color::rgb(0, 0);

ui::Theme theme()
{
    ui::Theme t = ui::default_theme();
    t.id = "prospero-midnight";
    t.name = "Midnight / Amber";
    t.page = kBackground;
    t.surface = kPanel;
    t.surface_high = kRaised;
    t.text = t.page_text = kInk;
    t.text_muted = t.page_text_muted = kMuted;
    t.primary = t.accent = t.focus = kAmber;
    t.on_primary = kBackground;
    t.secondary = kRaised;
    t.on_secondary = kInk;
    t.outline = kLine;
    t.light = Color::rgb(0xffffff, 0.18f);
    t.style = ui::SurfaceStyle::flat;
    t.radius = 16;
    t.radius_card = 22;
    t.border = 1;
    t.focus_width = 2;
    t.focus_gap = 5;
    t.omega = 20;
    t.damping = 1;
    return t;
}

// Contour sculpture made entirely from the kit's SDF lines. No image assets.
void sculpture(gfx::DrawList &list, float cx, float cy, float size, Color color, float time,
               int bands = 36)
{
    const float turn = 0.46f + 0.025f * std::sin(time * 0.35f);
    for (int band = 0; band < bands; ++band)
    {
        const float v = static_cast<float>(band) * 2 * kPi / static_cast<float>(bands);
        float last_x = 0, last_y = 0;
        const int segments = bands > 24 ? 72 : 36;
        for (int step = 0; step <= segments; ++step)
        {
            const float u = static_cast<float>(step) * 2 * kPi / static_cast<float>(segments);
            const float radius = size * (0.70f + 0.24f * std::cos(v));
            const float x = radius * std::cos(u);
            const float y = radius * std::sin(u);
            const float z = size * 0.24f * std::sin(v);
            const float tilt_y = y * 0.62f - z * 0.78f;
            const float depth = y * 0.78f + z * 0.62f;
            const float px = cx + x * std::cos(turn) - tilt_y * std::sin(turn);
            const float py = cy + x * std::sin(turn) + tilt_y * std::cos(turn);
            const float alpha = 0.22f + 0.70f * (depth / size + 1) * 0.5f;
            if (step)
                list.line(last_x, last_y, px, py, bands > 24 ? 1.25f : 1.0f,
                          color.with_alpha(alpha));
            last_x = px;
            last_y = py;
        }
    }
}

} // namespace

struct NativeUI::Impl
{
    Impl(NativeUI &owner, App &app, const ui::Fonts &fonts, gfx::Renderer &renderer)
        : owner_(owner), app_(app), fonts_(fonts), renderer_(renderer), theme_(theme())
    {
        tabs_.style.theme = theme_;
        tabs_.style.kind = ui::TabKind::underline;
        tabs_.style.track = false;
        tabs_.style.focus_ring = false;
        tabs_.style.on_page = true;
        tabs_.style.text_size = 25;
        tabs_.style.gap = 38;
        tabs_.style.padding = 8;
        tabs_.style.height = 62;
        tabs_.set_tabs(
            {{"Workspace", 0, false, 0}, {"Models", 0, false, 1}, {"Settings", 0, false, 2}});
        tabs_.set_bounds({652, 57, 760, 66});
        tabs_.set_focused(false);

        sessions_.style.theme = theme_;
        sessions_.style.title_size = 24;
        sessions_.style.subtitle_size = 20;
        sessions_.style.row_height = 92;
        sessions_.style.gap = 14;
        sessions_.style.padding = 20;
        sessions_.style.highlight.kind = ui::HighlightKind::bar;
        sessions_.style.highlight.color = kAmber.with_alpha(0.7f);
        sessions_.set_bounds({96, 354, 352, 456});
        sessions_.set_active(false);

        models_.style.theme = theme_;
        models_.style.columns = 3;
        models_.style.cell_height = 196;
        models_.style.gap_x = 24;
        models_.style.padding = 14;
        models_.style.card.text = ui::CardText::none;
        models_.style.card.art_aspect = 0;
        models_.style.card.focus_scale = 1.025f;
        models_.style.card.lift = 6;
        models_.style.card.glow = false;
        models_.style.card.plate = true;
        models_.set_bounds({82, 476, 1756, 452});
        models_.content = [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &,
                                 int index, float focus)
        { draw_model(canvas.list, cell, visible_models_[static_cast<std::size_t>(index)], focus); };

        filters_.style.theme = theme_;
        filters_.style.kind = ui::TabKind::segmented;
        filters_.style.width = ui::TabWidth::fill;
        filters_.style.height = 60;
        filters_.style.text_size = 23;
        filters_.style.wrap = true;
        filters_.set_tabs({{"All", 0, false, 0},
                           {"Text", 0, false, 1},
                           {"Image", 0, false, 2},
                           {"Audio", 0, false, 3},
                           {"Voice", 0, false, 4}});
        filters_.set_bounds({732, 386, 1092, 60});
        filters_.set_focused(false);
        search_.style.theme = theme_;
        search_.style.max_rows = 0;
        search_.set_bounds({96, 386, 596, 60});
        search_.set_placeholder("Search your models");
        keyboard_.style.theme = theme_;
        keyboard_.style.bindings = ui::KeyboardBindings::standard();
        keyboard_.style.max_length = 64;
        keyboard_.set_bounds({360, 596, 1200, 310});
        keyboard_.on_text = [this](std::string_view value)
        {
            search_.insert(value);
            refresh_models();
        };
        keyboard_.on_backspace = [this]
        {
            search_.backspace();
            refresh_models();
        };
        refresh_models();

        form_.style.theme = theme_;
        form_.style.row_height = 92;
        form_.style.gap = 12;
        form_.style.label_size = 27;
        form_.style.value_size = 24;
        form_.style.control_width = 296;
        form_.style.highlight.kind = ui::HighlightKind::ring;
        form_.style.description_inline = false;
        form_.style.dividers = true;
        form_.style.padding = 26;
        form_.add_choice(1, "Color theme", {"Midnight", "Daylight"});
        form_.add_choice(2, "Accent color", {"Amber", "Mist", "Sage"});
        form_.add_slider(3, "Interface sounds", 35, 0, 100, 5).unit = "%";
        form_.add_toggle(4, "Reduce motion", false);
        form_.add_toggle(5, "High contrast", false);
        form_.set_bounds({494, 348, 918, 528});

        dialog_.style.theme = theme_;
        dialog_.style.width = 830;
        dialog_.style.title_size = 42;
        dialog_.style.body_size = 28;
        dialog_.style.icon_size = 0;
        dialog_.style.padding = 48;
        dialog_.style.scrim = 0.70f;
        dialog_.style.frost = 0.85f;
        dialog_.style.centered = false;
        sessions_.set_bounds({96, 314, 352, 590});
        composer_.set_bounds({536, 848, 1258, 74});
        composer_.style.field_height = 72;
        composer_.style.max_length = 1023;
        composer_.style.counter = false;
        composer_.set_placeholder("Take an idea a little further...");
        chat_.set_bounds({536, 372, 1258, 402});
        chat_.style.clip_bleed = 0;
        categories_.style = sessions_.style;
        categories_.style.row_height = 76;
        categories_.set_bounds({96, 357, 324, 462});
        std::vector<ui::ListItem> categories;
        for (const char *name : {"Appearance", "Generation", "Sound", "Accessibility"})
        {
            ui::ListItem item;
            item.title = name;
            categories.push_back(std::move(item));
        }
        categories_.set_items(std::move(categories));
        categories_.set_active(false);
        build_form();
        apply_theme();
    }

    void update(const InputFrame &input, float dt, ui::Feedback &feedback)
    {
        const bool was_generating = app_.generating();
        app_.poll();
        if (was_generating && !app_.generating())
            feedback.play(app_.state().retry_available ? audio::Cue::error : audio::Cue::complete);
        if (revision_ != app_.state().revision)
            sync();
        upload_visible_image();
        clock_ += reduced_motion() ? 0 : dt;
        page_age_ += dt;
        if (keyboard_pending_)
        {
        }
        else if (search_open_)
        {
            const auto event = keyboard_.handle(input, feedback);
            if (event == ui::Event::activated || event == ui::Event::cancelled)
            {
                search_open_ = false;
                search_.set_active(false);
                feedback.play(audio::Cue::modal_close);
            }
        }
        else if (dialog_.is_open())
        {
            if (dialog_.handle(input, feedback) == ui::Event::activated && dialog_.choice() == 1)
            {
                bool accepted = false;
                if (dialog_action_ == 1)
                    accepted = app_.select_model(static_cast<unsigned>(dialog_index_));
                else if (dialog_action_ == 2)
                    accepted = app_.delete_session(static_cast<unsigned>(dialog_index_));
                else if (dialog_action_ == 3)
                {
                    quit_ = !app_.busy() && !app_.state().unsaved;
                    accepted = quit_;
                }
                feedback.play(accepted ? audio::Cue::select : audio::Cue::error);
            }
        }
        else if (input.is_pressed(Action::menu))
        {
            if (app_.busy() || app_.state().unsaved)
                feedback.play(audio::Cue::error);
            else
                open_dialog(3, -1, "Close ProsperoAI?",
                            "Close your local workspace and return to the home screen.",
                            "Close app", feedback);
        }
        else if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
        {
            if (tabs_.step(input.is_pressed(Action::page_next) ? 1 : -1, input, feedback) ==
                ui::Event::changed)
            {
                page_age_ = 0;
                models_.enter();
                form_.enter();
            }
        }
        else if (tabs_.active() == 1)
        {
            if (input.is_pressed(Action::north))
            {
                filters_.step(1, input, feedback);
                refresh_models();
            }
            else if (input.is_pressed(Action::west))
            {
                search_open_ = true;
                search_.set_active(true);
                keyboard_.set_length(search_.length());
                keyboard_.enter();
                feedback.play(audio::Cue::modal_open);
            }
            else if (models_.handle(input, feedback) == ui::Event::activated &&
                     !visible_models_.empty())
            {
                const int index = visible_models_[static_cast<std::size_t>(models_.focus())];
                if (app_.busy())
                    feedback.play(audio::Cue::error);
                else
                    open_dialog(1, index,
                                "Use " + app_.state().models[static_cast<std::size_t>(index)].name +
                                    "?",
                                "Your current conversation is kept. A new conversation starts with "
                                "this model.",
                                "Use model", feedback);
            }
        }
        else if (tabs_.active() == 2)
        {
            if (input.is_pressed(Action::back))
            {
                category_focus_ = true;
                feedback.play(audio::Cue::back);
            }
            else if (category_focus_)
            {
                const auto event = categories_.handle(input, feedback);
                if (event == ui::Event::moved)
                {
                    category_ = categories_.focus();
                    build_form();
                }
                if (event == ui::Event::activated || input.nav == Direction::right)
                    category_focus_ = false;
            }
            else if (form_.handle(input, feedback) == ui::Event::changed)
                apply_form();
            categories_.set_active(category_focus_);
        }
        else
        {
            chat_.handle(input, feedback);
            if (input.is_pressed(Action::west))
            {
                if (app_.new_session())
                {
                    conversation_ = true;
                    composer_.clear();
                    page_age_ = 0;
                    feedback.play(audio::Cue::open);
                }
                else
                    feedback.play(audio::Cue::error);
            }
            else if (input.nav == Direction::left)
            {
                if (!conversation_)
                    page_age_ = 0;
                conversation_ = true;
                rail_ = true;
                feedback.play(audio::Cue::focus);
            }
            else if (input.nav == Direction::right || input.is_pressed(Action::back))
                rail_ = false;
            else if (rail_)
            {
                if (input.is_pressed(Action::north) && sessions_.focus() > 0 && !app_.busy())
                {
                    const int index = sessions_.focus() - 1;
                    open_dialog(
                        2, index, "Delete this conversation?",
                        "The conversation and its saved media will be removed from this console.",
                        "Delete", feedback);
                }
                else if (sessions_.handle(input, feedback) == ui::Event::activated)
                {
                    const bool accepted =
                        sessions_.focus() == 0
                            ? app_.new_session()
                            : app_.open_session(static_cast<unsigned>(sessions_.focus() - 1));
                    if (accepted)
                    {
                        conversation_ = true;
                        rail_ = false;
                        composer_.clear();
                        page_age_ = 0;
                    }
                    else
                        feedback.play(audio::Cue::error);
                }
            }
            else if (input.is_pressed(Action::confirm))
                submit(feedback);
            else if (input.is_pressed(Action::north))
            {
                const bool accepted =
                    app_.state().retry_available ? app_.retry() : app_.play_audio();
                feedback.play(accepted ? audio::Cue::select : audio::Cue::error);
            }
            else if (input.nav == Direction::up)
                chat_.scroll_by(0, -120);
            else if (input.nav == Direction::down)
                chat_.scroll_by(0, 120);
        }
        sessions_.set_active(rail_);
        composer_.set_active(!rail_ && !app_.busy());
        composer_.set_disabled(app_.busy());
        tabs_.update(dt);
        sessions_.update(dt);
        categories_.update(dt);
        models_.update(dt);
        filters_.update(dt);
        search_.update(dt);
        keyboard_.update(dt);
        form_.update(dt);
        dialog_.update(dt);
        composer_.update(dt);
        chat_.update(dt);
    }

    void draw(UiFrame &frame) const
    {
        frame.backdrop.mode = gfx::BackdropMode::gradient;
        frame.backdrop.colors[0] = kBackground;
        frame.backdrop.colors[1] = kBackground;
        frame.backdrop.colors[2] = gfx::mix(kBackground, kAmber, 0.15f);
        frame.backdrop.params[0] = 0.77f;
        frame.backdrop.params[1] = 0.34f;
        frame.backdrop.params[2] = tabs_.active() == 0 && !conversation_ ? 0.24f : 0.07f;
        auto &list = frame.scene;
        ui::Canvas canvas{list, fonts_, 0, clock_};
        draw_header(canvas);
        const float entrance =
            reduced_motion() ? 1 : tween::cubic_out(std::min(page_age_ / 0.42f, 1.0f));
        list.push_transform(1, 0, 0, 0, (1 - entrance) * 18);
        list.push_opacity(entrance);
        if (tabs_.active() == 1)
            draw_models(canvas);
        else if (tabs_.active() == 2)
            draw_settings(canvas);
        else if (conversation_)
            draw_conversation(canvas);
        else
            draw_welcome(canvas);
        list.pop_opacity();
        list.pop_transform();
        draw_footer(list);
        if (search_open_)
        {
            auto &overlay = frame.overlay;
            overlay.rounded_rect({0, 0, 1920, 1080}, 0, Color::rgb(0, 0.72f));
            overlay.bordered_rect({324, 452, 1272, 488}, 24, kPanel, 1, kLine);
            text(overlay, "Search models", 364, 506, 31, kInk, true);
            text(overlay, search_.text().empty() ? "Type a name..." : search_.text().c_str(), 364,
                 560, 28, kAmber);
            ui::Canvas keys{overlay, fonts_, 0, clock_};
            keyboard_.draw(keys);
        }
        if (dialog_.visible())
        {
            frame.glass = true;
            ui::Canvas overlay{frame.overlay, fonts_, frame.glass_texture, clock_};
            dialog_.draw(overlay);
        }
    }

  public:
    bool reduced_motion() const
    {
        return app_.state().preferences.reduced_motion;
    }
    void refresh_models()
    {
        visible_models_.clear();
        std::string query = search_.text();
        const auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        std::transform(query.begin(), query.end(), query.begin(), lower);
        const auto &catalog = app_.state().models;
        for (std::size_t i = 0; i < catalog.size(); ++i)
        {
            const int capability = static_cast<int>(catalog[i].capability) + 1;
            std::string name = catalog[i].name + " " + catalog[i].id;
            std::transform(name.begin(), name.end(), name.begin(), lower);
            if ((filters_.active() == 0 || filters_.active() == capability) &&
                name.find(query) != std::string::npos)
                visible_models_.push_back(static_cast<int>(i));
        }
        models_.set_count(static_cast<int>(visible_models_.size()));
        models_.set_focus(0);
        models_.enter();
    }
    void text(gfx::DrawList &list, const char *value, float x, float y, float size,
              Color color = Color::rgb(0, -1), bool bold = false) const
    {
        ui::text(list, bold ? fonts_.semibold : fonts_.regular, value, x, y, size,
                 color.a < 0 ? kInk : color);
    }
    void label(gfx::DrawList &list, const char *value, float x, float y,
               Color color = Color::rgb(0, -1)) const
    {
        ui::text(list, fonts_.semibold, value, x, y, 16, color.a < 0 ? kMuted : color,
                 gfx::Align::left, 2.4f);
    }
    void mark(gfx::DrawList &list, float cx, float cy, float scale = 1) const
    {
        for (int i = 0; i < 4; ++i)
            list.arc(cx, cy, (10 + static_cast<float>(i) * 3) * scale, 1.4f * scale,
                     -0.4f + static_cast<float>(i) * 0.24f, 4.7f, kAmber);
    }
    void draw_header(ui::Canvas &canvas) const
    {
        auto &list = canvas.list;
        mark(list, 119, 91, 1.05f);
        text(list, "ProsperoAI", 159, 101, 29, kInk, true);
        tabs_.draw(canvas);
        list.circle(1620, 90, 4, kGreen);
        text(list, "On your console", 1638, 98, 22, kMuted);
        list.line(96, 150, 1824, 150, 1, kLine);
    }
    void draw_footer(gfx::DrawList &list) const
    {
        list.line(96, 966, 1824, 966, 1, kLine);
        const bool library = tabs_.active() == 1, settings = tabs_.active() == 2;
        const char *action = library                    ? "Choose"
                             : settings                 ? "Adjust"
                             : rail_                    ? "Open"
                             : composer_.text().empty() ? "Write"
                                                        : "Send";
        ui::Hint hints[5] = {{ui::Button::cross, action},
                             {ui::Button::circle, "Back"},
                             {ui::Button::l1, "Change view", ui::Button::r1}};
        std::size_t count = 3;
        if (!settings)
        {
            const auto &messages = app_.state().messages;
            const bool audio = std::any_of(
                messages.begin(), messages.end(), [](const auto &m)
                { return std::string_view(m.content).find(".wav") != std::string_view::npos; });
            const char *triangle = library ? "Filter"
                                   : rail_ ? (sessions_.focus() > 0 ? "Delete" : nullptr)
                                   : app_.state().retry_available ? "Retry"
                                   : audio                        ? "Play audio"
                                                                  : nullptr;
            if (triangle)
                hints[count++] = {ui::Button::triangle, triangle};
            hints[count++] = {ui::Button::square, library ? "Search" : "New"};
        }
        ui::HintLayout layout;
        layout.size = 30;
        layout.text_size = 22;
        layout.cy = 1010;
        layout.item_gap = 32;
        ui::draw_hints(list, fonts_, ui::GlyphStyle::mono(kInk, kRaised), hints, count, 96, false,
                       layout);
        label(list, "PRIVATE BY DESIGN", 1543, 1017);
    }
    void draw_welcome(ui::Canvas &canvas) const
    {
        auto &list = canvas.list;
        label(list, "A LITTLE CURIOSITY. ENDLESS POSSIBILITY.", 120, 254, kAmber);
        ui::text(list, fonts_.display, "Big ideas.", 114, 382, 96, kInk);
        ui::text(list, fonts_.display, "Right here.", 114, 496, 96, kAmber);
        text(list, "A space to think, imagine, and create.", 120, 560, 29, kMuted);
        text(list, "Powered by your PS5. Entirely yours.", 120, 604, 29, kMuted);
        ui::Painter paint(list, fonts_, theme_);
        paint.button({120, 656, 342, 72}, "Open your workspace", ui::ButtonKind::primary,
                     {1, 0, false});
        text(list, fit(app_.state().status, 23, 600).c_str(), 491, 702, 23, kMuted);
        list.glow({1260, 354, 280, 196}, 100, 90, kAmber.with_alpha(0.06f));
        sculpture(list, 1368, 446, 320, kAmber, clock_, 48);
        label(list, "THOUGHT, TAKING SHAPE", 1214, 744, kMuted);
        list.line(120, 786, 1800, 786, 1, kLine);
        const char *names[] = {"Converse", "Imagine", "Compose", "Speak"};
        const char *subtitles[] = {"Find a new perspective", "Create something unseen",
                                   "Explore sounds and textures", "Give your words a voice"};
        for (int i = 0; i < 4; ++i)
        {
            const float x = 120 + static_cast<float>(i) * 434;
            label(list,
                  i == 0   ? "01 / TEXT"
                  : i == 1 ? "02 / IMAGE"
                  : i == 2 ? "03 / SOUND"
                           : "04 / VOICE",
                  x, 828);
            text(list, names[i], x, 873, 32, kInk, true);
            text(list, subtitles[i], x, 913, 22, kMuted);
        }
    }
    void draw_conversation(ui::Canvas &canvas) const
    {
        auto &list = canvas.list;
        label(list, "YOUR SPACE", 116, 232);
        text(list, "Conversations", 116, 281, 34, kInk, true);
        sessions_.draw(canvas);
        list.line(482, 214, 482, 916, 1, kLine);
        const auto &state = app_.state();
        const char *name = state.session.id[0] ? state.session.model_name : current_name();
        label(list, "WORKSPACE", 536, 232, kAmber);
        text(list,
             fit(state.session.id[0] ? state.session.title : "A new possibility", 50, 1210).c_str(),
             536, 293, 50, kInk, true);
        text(list, fit(name, 23, 1200).c_str(), 536, 337, 23, kMuted);
        chat_.begin(canvas);
        for (const auto &row : chat_rows_)
        {
            if (row.y + row.height < chat_.offset_y() ||
                row.y > chat_.offset_y() + chat_.bounds().h)
                continue;
            const bool user = row.role == "user";
            if (user)
                list.bordered_rect({200, row.y, 1034, row.height - 22}, 22, kRaised, 1, kLine);
            else
            {
                mark(list, 22, row.y + 24, 0.8f);
                text(list, name, 60, row.y + 33, 24, kInk, true);
            }
            float y = row.y + (user ? 42 : 84);
            for (const auto &line : row.lines)
            {
                text(list, line.c_str(), user ? 230 : 0, y, body_size_, user ? kInk : kMuted);
                y += body_size_ * 1.48f;
            }
            if (!row.image_path.empty())
            {
                const auto texture = images_.find(row.image_path);
                if (texture != images_.end())
                    list.image(texture->second.id,
                               {0, y, std::min(1214.0f, 360 * texture->second.aspect),
                                std::min(360.0f, 1214 / texture->second.aspect)},
                               {0, 0, 1, 1}, Color::rgb(0xffffff), 18);
                else
                    text(list, "Image preview unavailable", 0, y + 44, 24, kMuted);
            }
            else if (row.audio)
            {
                list.bordered_rect({0, y, 670, 88}, 18, kPanel, 1, kLine);
                ui::draw_button(list, fonts_, ui::GlyphStyle::mono(kAmber, kRaised),
                                ui::Button::triangle, 42, y + 44, 30);
                text(list, "Play saved audio", 80, y + 53, 26, kInk, true);
            }
        }
        if (chat_rows_.empty())
        {
            text(list, "What would you like to explore?", 0, 95, 37, kInk, true);
            text(list, "Your ideas stay on this console.", 0, 151, 28, kMuted);
        }
        chat_.end(canvas);
        chat_.draw(canvas);
        composer_.draw(canvas);
        if (app_.busy())
        {
            const float rotation = reduced_motion() ? 0 : clock_ * 2;
            list.arc(1770, 814, 9, 2, rotation, 4.5f, kAmber);
        }
        text(list, fit(state.status, 21, 1190).c_str(), 536, 814, 21, kMuted);
    }
    void draw_models(ui::Canvas &canvas) const
    {
        auto &list = canvas.list;
        label(list, "YOUR MODELS / YOUR POSSIBILITIES", 96, 221, kAmber);
        text(list, "A world of possibilities.", 92, 297, 65, kInk, true);
        text(list, "One library for conversation, images, sound, and voice.", 96, 344, 27, kMuted);
        list.circle(1511, 255, 5, kGreen);
        text(list, "ACTIVE MODEL", 1530, 262, 17, kMuted, true);
        text(list, fit(current_name(), 29, 314).c_str(), 1508, 305, 29, kInk, true);
        search_.draw(canvas);
        filters_.draw(canvas);
        models_.draw(canvas);
        if (visible_models_.empty())
            text(list, "No models match. Try another name or capability.", 96, 574, 28, kMuted);
        char count[96];
        std::snprintf(count, sizeof(count), "%zu models shown  /  %zu installed",
                      visible_models_.size(), app_.state().models.size());
        text(list, count, 96, 936, 23, kMuted);
        text(list, "Conversations remember their model.", 1334, 936, 23, kMuted);
    }
    void draw_model(gfx::DrawList &list, const Rect &r, int index, float focus) const
    {
        const auto &model = app_.state().models[static_cast<std::size_t>(index)];
        const Color accents[] = {kAmber, Color::rgb(0xc8afe4), Color::rgb(0x8fbfb7),
                                 Color::rgb(0xe0b3a9)};
        const char *kinds[] = {"CONVERSATION", "IMAGE", "SOUND", "VOICE"};
        const int kind = static_cast<int>(model.capability);
        const Color accent = app_.state().preferences.theme ? kAmber : accents[kind];
        list.bordered_rect(r, 22, gfx::mix(kPanel, kRaised, focus * 0.4f), 1, kLine);
        list.push_clip(r.inset(1));
        const float cx = r.x + 97, cy = r.cy();
        list.gradient_rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, 22, accent.with_alpha(0.08f),
                           kClear);
        if (kind == 0)
            sculpture(list, cx, cy, 78, accent, clock_ + static_cast<float>(index) * 12, 18);
        else if (kind == 1)
        {
            for (int i = 0; i < 7; ++i)
            {
                const float f = static_cast<float>(i);
                list.rotated_rect({cx - 48 + f * 3, cy - 48 + f * 2, 92 - f * 5, 92 - f * 5}, 12,
                                  -0.4f + f * 0.13f, accent.with_alpha(0.14f + f * 0.05f));
            }
        }
        else
        {
            for (int i = 0; i < 25; ++i)
            {
                const float x = static_cast<float>(i - 12);
                const float h =
                    12 +
                    52 * std::exp(-x * x / 68) *
                        (0.48f + 0.52f * std::abs(std::sin(x * 0.7f + static_cast<float>(index))));
                list.rounded_rect({cx + x * 5 - 1.5f, cy - h, 3, h * 2}, 1.5f,
                                  accent.with_alpha(0.6f + 0.4f * focus));
            }
        }
        label(list, kinds[kind], r.x + 196, r.y + 55, accent);
        text(list, fit(model.name, 30, r.w - 224).c_str(), r.x + 196, r.y + 97, 30, kInk, true);
        text(list, fit(model.id, 20, r.w - 224).c_str(), r.x + 196, r.y + 133, 20, kMuted);
        if (index == active_model_)
        {
            list.circle(r.x + 202, r.y + 163, 4, kGreen);
            text(list, app_.state().ready ? "Selected" : "Needs attention", r.x + 216, r.y + 171,
                 20, kGreen);
        }
        list.pop_clip();
    }
    void draw_settings(ui::Canvas &canvas) const
    {
        auto &list = canvas.list;
        label(list, "SETTINGS", 96, 228, kAmber);
        text(list, "Make it feel like yours.", 92, 311, 65, kInk, true);
        categories_.draw(canvas);
        list.line(452, 356, 452, 896, 1, kLine);
        form_.draw(canvas);
        list.bordered_rect({1470, 352, 354, 510}, 24, kPanel, 1, kLine);
        label(list, "LIVE PREVIEW", 1500, 395, kAmber);
        sculpture(list, 1647, 503, 82, kAmber, clock_, 24);
        text(list, "Quietly alive.", 1500, 633, 31, kInk, true);
        ui::paragraph(list, fonts_.regular,
                      "Soft light. A focus that follows you. Just enough motion to feel natural.",
                      1500, 684, 25, 294, 37, kMuted, 4);
        text(list, fit(app_.state().status, 23, 1310).c_str(), 494, 924, 23, kMuted);
    }

    const char *current_name() const
    {
        const auto &state = app_.state();
        return state.selected_model < 0
                   ? "Choose a model"
                   : state.models[static_cast<std::size_t>(state.selected_model)].name.c_str();
    }
    std::string fit(std::string_view value, float size, float width) const
    {
        return fonts_.semibold.font->fit(value, size, width);
    }
    void open_dialog(int action, int index, std::string title, std::string body, const char *button,
                     ui::Feedback &feedback)
    {
        dialog_action_ = action;
        dialog_index_ = index;
        ui::DialogContent content;
        content.title = std::move(title);
        content.body = std::move(body);
        content.buttons = {{"Keep current", ui::ButtonKind::secondary, false},
                           {button, ui::ButtonKind::primary, false}};
        dialog_.open(std::move(content), feedback);
    }
    void type(char character, ui::Feedback &feedback)
    {
        if (dialog_.is_open() || keyboard_pending_)
            return;
        if (tabs_.active() == 1)
        {
            search_.insert(character, feedback);
            refresh_models();
        }
        else if (tabs_.active() == 0 && !app_.busy())
        {
            conversation_ = true;
            rail_ = false;
            composer_.insert(character, feedback);
        }
    }
    void backspace(ui::Feedback &feedback)
    {
        if (dialog_.is_open() || keyboard_pending_)
            return;
        if (tabs_.active() == 1)
        {
            search_.backspace(feedback);
            refresh_models();
        }
        else if (tabs_.active() == 0 && !app_.busy())
            composer_.backspace(feedback);
    }
    void submit(ui::Feedback &feedback)
    {
        if (search_open_)
        {
            search_open_ = false;
            search_.set_active(false);
            feedback.play(audio::Cue::modal_close);
            return;
        }
        if (tabs_.active() != 0 || app_.busy() || dialog_.is_open() || keyboard_pending_)
            return;
        if (!app_.can_send())
        {
            tabs_.set_active(1);
            page_age_ = 0;
            feedback.play(audio::Cue::tab);
            return;
        }
        conversation_ = true;
        rail_ = false;
        if (!composer_.text().empty())
        {
            if (app_.send(composer_.text()))
            {
                composer_.clear();
                feedback.play(audio::Cue::select);
                chat_.scroll_to(0, chat_.max_y());
            }
            else
                feedback.play(audio::Cue::error);
        }
        else if (owner_.request_keyboard)
        {
            keyboard_pending_ = true;
            owner_.request_keyboard(composer_.text());
            feedback.play(audio::Cue::open);
        }
    }
    void keyboard_result(const char *value)
    {
        keyboard_pending_ = false;
        if (value && *value)
        {
            composer_.set_text(value);
            // The OS keyboard's Done action sends, matching the original app.
            if (app_.send(composer_.text()))
                composer_.clear();
        }
    }
    void build_form()
    {
        const auto &p = app_.state().preferences;
        form_.clear();
        if (category_ == 0)
        {
            form_.add_choice(1, "Color theme", {"Midnight", "Daylight"}, static_cast<int>(p.theme));
            form_.add_choice(2, "Accent color", {"Amber", "Mist", "Sage"},
                             static_cast<int>(p.accent));
        }
        else if (category_ == 1)
        {
            form_.add_choice(6, "Response style", {"Balanced", "Precise", "Creative"},
                             static_cast<int>(p.style));
            form_.add_choice(7, "Response length",
                             {"64 tokens", "128 tokens", "192 tokens", "256 tokens"},
                             static_cast<int>(p.output_limit / 64 - 1));
            form_.add_value(9, "Processing", "On this console");
        }
        else if (category_ == 2)
            form_.add_slider(3, "Interface sounds", static_cast<float>(p.volume), 0, 100, 5).unit =
                "%";
        else
        {
            form_.add_toggle(4, "Reduce motion", p.reduced_motion);
            form_.add_toggle(5, "High contrast", p.high_contrast);
            form_.add_choice(8, "Reading size", {"Standard", "Large"},
                             static_cast<int>(p.text_size));
        }
        form_.enter();
    }
    void apply_form()
    {
        auto p = app_.state().preferences;
        if (category_ == 0)
        {
            p.theme = static_cast<unsigned>(form_.choice_index(1));
            p.accent = static_cast<unsigned>(form_.choice_index(2));
        }
        else if (category_ == 1)
        {
            p.style = static_cast<unsigned>(form_.choice_index(6));
            p.output_limit = static_cast<unsigned>(form_.choice_index(7) + 1) * 64;
        }
        else if (category_ == 2)
            p.volume = static_cast<unsigned>(form_.slider_value(3));
        else
        {
            p.reduced_motion = form_.toggle_value(4);
            p.high_contrast = form_.toggle_value(5);
            p.text_size = static_cast<unsigned>(form_.choice_index(8));
        }
        app_.set_preferences(std::move(p));
        apply_theme();
    }
    void apply_theme()
    {
        const auto &p = app_.state().preferences;
        const bool day = p.theme == 1;
        kBackground = Color::rgb(day ? 0xf2eee6 : 0x0c0e12);
        kPanel = Color::rgb(day ? 0xfffcf6 : 0x15191f);
        kRaised = Color::rgb(day ? 0xe5dfd5 : 0x20252c);
        kInk = Color::rgb(day ? 0x20252c : 0xf4f1ea);
        kMuted = p.high_contrast ? kInk : Color::rgb(day ? 0x555d68 : 0x9ca4b0);
        kLine = kInk.with_alpha(p.high_contrast ? 0.65f : 0.09f);
        const Color night_accents[] = {Color::rgb(0xf0be7d), Color::rgb(0x9ebadf),
                                       Color::rgb(0xa2c7ad)};
        const Color day_accents[] = {Color::rgb(0x865021), Color::rgb(0x355a89),
                                     Color::rgb(0x37674a)};
        kAmber = day ? day_accents[p.accent] : night_accents[p.accent];
        kGreen = Color::rgb(day ? 0x37674a : 0xa2c7ad);
        theme_ = theme();
        theme_.page = kBackground;
        theme_.surface = kPanel;
        theme_.surface_high = kRaised;
        theme_.text = theme_.page_text = kInk;
        theme_.text_muted = theme_.page_text_muted = kMuted;
        theme_.primary = theme_.accent = theme_.focus = kAmber;
        theme_.on_primary = day ? Color::rgb(0xffffff) : kBackground;
        theme_.secondary = kRaised;
        theme_.on_secondary = kInk;
        theme_.outline = kLine;
        theme_.focus_width = p.high_contrast ? 3 : 2;
        const std::initializer_list<ui::ComponentStyle *> styles{
            &tabs_.style,   &filters_.style,  &sessions_.style, &categories_.style,
            &models_.style, &search_.style,   &keyboard_.style, &form_.style,
            &dialog_.style, &composer_.style, &chat_.style};
        for (ui::ComponentStyle *style : styles)
        {
            style->theme = theme_;
            style->reduced_motion = p.reduced_motion;
        }
        sessions_.style.highlight.color = categories_.style.highlight.color =
            kAmber.with_alpha(0.7f);
    }
    void sync()
    {
        const auto &state = app_.state();
        revision_ = state.revision;
        for (auto it = images_.begin(); it != images_.end();)
        {
            const bool retained =
                std::any_of(state.images.begin(), state.images.end(),
                            [&](const auto &image) { return image->path == it->first; });
            if (retained)
                ++it;
            else
            {
                glDeleteTextures(1, &it->second.id);
                it = images_.erase(it);
            }
        }
        active_model_ = state.selected_model;
        if (active_model_ >= 0)
        {
            const char *prompts[] = {
                "Take an idea a little further...", "Describe an image you want to create...",
                "Describe a sound or atmosphere...", "Write the words you would like to hear..."};
            composer_.set_placeholder(prompts[static_cast<unsigned>(
                state.models[static_cast<std::size_t>(active_model_)].capability)]);
        }
        apply_theme();
        if (catalog_count_ != static_cast<int>(state.models.size()))
        {
            catalog_count_ = static_cast<int>(state.models.size());
            refresh_models();
            build_form();
        }
        bool sessions_changed = sessions_.items().size() != state.sessions.size() + 1;
        if (!sessions_changed)
            for (std::size_t i = 0; i < state.sessions.size(); ++i)
                if (sessions_.items()[i + 1].title != state.sessions[i].title ||
                    sessions_.items()[i + 1].subtitle != state.sessions[i].model_name)
                    sessions_changed = true;
        if (sessions_changed)
        {
            const int focus = sessions_.focus();
            std::vector<ui::ListItem> rows;
            ui::ListItem first;
            first.title = "+ New conversation";
            rows.push_back(std::move(first));
            for (const auto &session : state.sessions)
            {
                ui::ListItem item;
                item.title = session.title;
                item.subtitle = session.model_name;
                rows.push_back(std::move(item));
            }
            sessions_.set_items(std::move(rows));
            sessions_.set_focus(focus);
        }
        const bool follow = chat_.max_y() - chat_.offset_y() < 80;
        const float size = state.preferences.text_size ? 34.0f : 28.0f;
        const bool resize = body_size_ != size;
        body_size_ = size;
        const bool streaming = app_.generating() && !state.stream.empty();
        chat_rows_.resize(state.messages.size() + (streaming ? 1 : 0));
        float y = 0;
        for (std::size_t i = 0; i < chat_rows_.size(); ++i)
        {
            auto &row = chat_rows_[i];
            const char *value =
                i < state.messages.size() ? state.messages[i].content : state.stream.c_str();
            const char *role = i < state.messages.size() ? state.messages[i].role : "assistant";
            if (resize || row.text != value || row.role != role)
            {
                row.text = value;
                row.role = role;
                row.image_path.clear();
                row.audio = false;
                std::string visible = row.text;
                if (row.role == "assistant")
                {
                    const auto start = visible.find("/download0/");
                    const auto end =
                        start == std::string::npos ? start : visible.find(".tga", start);
                    if (end != std::string::npos)
                        row.image_path = visible.substr(start, end + 4 - start);
                    row.audio = start != std::string::npos &&
                                visible.find(".wav", start) != std::string::npos;
                    if (!row.image_path.empty() || row.audio)
                        visible.resize(start);
                    while (!visible.empty() && (visible.back() == '\n' || visible.back() == '\r'))
                        visible.pop_back();
                }
                row.lines =
                    fonts_.regular.font->wrap(visible, body_size_, row.role == "user" ? 968 : 1214);
                row.height = (row.role == "user" ? 62 : 104) +
                             static_cast<float>(row.lines.size()) * body_size_ * 1.48f;
                if (!row.image_path.empty())
                    row.height += 382;
                else if (row.audio)
                    row.height += 110;
            }
            row.y = y;
            y += row.height;
        }
        chat_.set_content_size(1258, y);
        if (follow)
            chat_.scroll_to(0, chat_.max_y());
        if (!state.messages.empty())
            conversation_ = true;
    }

    void upload_visible_image()
    {
        if (tabs_.active() != 0 || !conversation_)
            return;
        for (const auto &row : chat_rows_)
        {
            if (row.image_path.empty() || images_.count(row.image_path) ||
                row.y + row.height < chat_.offset_y() ||
                row.y > chat_.offset_y() + chat_.bounds().h)
                continue;
            for (const auto &image : app_.state().images)
            {
                if (image->path != row.image_path)
                    continue;
                const auto texture = renderer_.batch().create_texture(
                    static_cast<int>(image->width), static_cast<int>(image->height),
                    image->rgba.data());
                if (texture)
                    images_.emplace(image->path,
                                    Texture{texture, static_cast<float>(image->width) /
                                                         static_cast<float>(image->height)});
                return; // At most one bounded upload per frame.
            }
        }
    }
    void release()
    {
        for (const auto &[path, texture] : images_)
            glDeleteTextures(1, &texture.id);
        images_.clear();
    }

    NativeUI &owner_;
    App &app_;
    const ui::Fonts &fonts_;
    gfx::Renderer &renderer_;
    ui::Theme theme_;
    Color kBackground = Color::rgb(0x0c0e12), kPanel = Color::rgb(0x15191f),
          kRaised = Color::rgb(0x20252c);
    Color kInk = Color::rgb(0xf4f1ea), kMuted = Color::rgb(0x9ca4b0),
          kLine = Color::rgb(0xffffff, 0.09f);
    Color kAmber = Color::rgb(0xf0be7d), kGreen = Color::rgb(0xa2c7ad);
    ui::TabBar tabs_, filters_;
    ui::ListView sessions_, categories_;
    ui::GridView models_;
    ui::SearchField search_;
    ui::Keyboard keyboard_;
    ui::Form form_;
    ui::Dialog dialog_;
    ui::TextField composer_;
    ui::ScrollArea chat_;
    std::vector<int> visible_models_;
    struct ChatRow
    {
        std::string text, role, image_path;
        bool audio = false;
        std::vector<std::string> lines;
        float y = 0, height = 0;
    };
    struct Texture
    {
        GLuint id;
        float aspect;
    };
    std::map<std::string, Texture> images_;
    std::vector<ChatRow> chat_rows_;
    float clock_ = 0, page_age_ = 1, body_size_ = 28;
    bool conversation_ = false, search_open_ = false, rail_ = false, category_focus_ = false;
    bool keyboard_pending_ = false, quit_ = false;
    unsigned revision_ = ~0U;
    int active_model_ = -1, catalog_count_ = -1, dialog_action_ = 0, dialog_index_ = -1;
    int category_ = 0;
};

NativeUI::NativeUI(App &app, const ui::Fonts &fonts, gfx::Renderer &renderer)
    : impl_(std::make_unique<Impl>(*this, app, fonts, renderer))
{
}
NativeUI::~NativeUI()
{
    release();
}
void NativeUI::release()
{
    impl_->release();
}
void NativeUI::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    impl_->update(input, dt, feedback);
}
void NativeUI::draw(UiFrame &frame) const
{
    impl_->draw(frame);
}
void NativeUI::type(char character, ui::Feedback &feedback)
{
    impl_->type(character, feedback);
}
void NativeUI::backspace(ui::Feedback &feedback)
{
    impl_->backspace(feedback);
}
void NativeUI::submit(ui::Feedback &feedback)
{
    impl_->submit(feedback);
}
void NativeUI::scroll(float pixels)
{
    impl_->chat_.scroll_by(0, pixels);
}
void NativeUI::keyboard_result(const char *text)
{
    impl_->keyboard_result(text);
}
bool NativeUI::quit_requested() const
{
    return impl_->quit_;
}
} // namespace prospero
