// ProsperoAI native frontend: palette, theme and the small shapes every screen shares.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_ui_impl.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace prospero
{
Palette make_palette(const Preferences &p)
{
    Palette c;
    c.day = p.theme == 1;
    const bool contrast = p.high_contrast;
    const Color night_accents[] = {Color::rgb(0xf0be7d), Color::rgb(0x9ebadf),
                                   Color::rgb(0xa2c7ad)};
    const Color day_accents[] = {Color::rgb(0x865021), Color::rgb(0x355a89), Color::rgb(0x37674a)};
    c.accent = c.day ? day_accents[p.accent] : night_accents[p.accent];
    c.page_top = Color::rgb(c.day ? 0xf6f1e8 : 0x06080d);
    c.page_bottom = Color::rgb(c.day ? 0xece5d9 : 0x0c1018);
    // Panels are tinted, not solid: the backdrop's light shows through them. High
    // contrast closes them, so text never sits on a moving colour.
    c.panel = Color::rgb(c.day ? 0xfffdf8 : 0x171c25, contrast ? 1.0f : c.day ? 0.80f : 0.72f);
    c.raised = Color::rgb(c.day ? 0xe6dfd3 : 0x262d39, contrast ? 1.0f : 0.90f);
    c.ink = Color::rgb(c.day ? 0x1d2229 : 0xf4f1ea);
    c.muted = contrast ? c.ink : Color::rgb(c.day ? 0x555d68 : 0x9ca4b0);
    c.line = c.ink.with_alpha(contrast ? 0.65f : c.day ? 0.12f : 0.10f);
    c.on_accent = c.day ? Color::rgb(0xffffff) : Color::rgb(0x0c0e12);
    c.good = Color::rgb(c.day ? 0x2f7350 : 0x8fd3a6);
    c.warn = Color::rgb(c.day ? 0x9a6412 : 0xf2c078);
    c.bad = Color::rgb(c.day ? 0xb93a3f : 0xf08a84);
    c.depth = Color::rgb(c.day ? 0xbfd0e6 : 0x35568f);
    c.kinds[0] = c.accent;
    c.kinds[1] = Color::rgb(c.day ? 0x6b4aa0 : 0xc8afe4);
    c.kinds[2] = Color::rgb(c.day ? 0x2c6f68 : 0x8fbfb7);
    c.kinds[3] = Color::rgb(c.day ? 0xa0584a : 0xe0b3a9);
    return c;
}

ui::Theme make_theme(const Palette &c, const Preferences &p)
{
    ui::Theme t = ui::default_theme();
    t.id = c.day ? "prospero-daylight" : "prospero-midnight";
    t.name = c.day ? "Daylight" : "Midnight";
    t.page = c.page_bottom;
    t.surface = c.panel;
    t.surface_high = c.raised;
    t.text = t.page_text = c.ink;
    t.text_muted = t.page_text_muted = c.muted;
    t.primary = t.accent = t.focus = c.accent;
    t.on_primary = c.on_accent;
    t.secondary = c.raised;
    t.on_secondary = c.ink;
    t.outline = c.line;
    t.light = Color::rgb(0xffffff, c.day ? 0.6f : 0.18f);
    t.shadow = Color::rgb(0x000000, c.day ? 0.18f : 0.5f);
    t.success = c.good;
    t.warning = c.warn;
    t.danger = c.bad;
    t.style = ui::SurfaceStyle::flat;
    t.radius = 16;
    t.radius_card = 22;
    t.border = 1;
    t.focus_width = p.high_contrast ? 3.0f : 2.0f;
    t.focus_gap = 5;
    t.omega = 20;
    t.damping = 1;
    t.sounds = audio::SoundSet::glass;
    t.dark = !c.day;
    return t;
}

void draw_sculpture(gfx::DrawList &list, float cx, float cy, float size, Color color, float time,
                    int bands)
{
    const float turn = 0.46f + 0.025f * std::sin(time * 0.35f);
    const int segments = bands > 24 ? 72 : 36;
    const float width = bands > 24 ? 1.25f : 1.0f;
    for (int band = 0; band < bands; ++band)
    {
        const float v = static_cast<float>(band) * 2 * kPi / static_cast<float>(bands);
        float last_x = 0, last_y = 0;
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
                list.line(last_x, last_y, px, py, width, color.with_alpha(alpha));
            last_x = px;
            last_y = py;
        }
    }
}

void draw_mark(gfx::DrawList &list, float cx, float cy, float scale, Color color)
{
    for (int i = 0; i < 4; ++i)
        list.arc(cx, cy, (10 + static_cast<float>(i) * 3) * scale, 1.4f * scale,
                 -0.4f + static_cast<float>(i) * 0.24f, 4.7f, color);
}

float NativeUI::Impl::text(gfx::DrawList &list, std::string_view value, float x, float y,
                           float size, Color color, bool bold, gfx::Align align) const
{
    return ui::text(list, bold ? fonts_.semibold : fonts_.regular, value, x, y, size, color, align);
}

void NativeUI::Impl::label(gfx::DrawList &list, std::string_view value, float x, float y,
                           Color color, gfx::Align align) const
{
    ui::text(list, fonts_.semibold, value, x, y, 16, color, align, 2.4f);
}

void NativeUI::Impl::panel(gfx::DrawList &list, const Rect &r, float radius, float lit) const
{
    if (!palette_.day)
        list.shadow({r.x, r.y + 10, r.w, r.h}, radius, 34, Color::rgb(0x000000, 0.22f));
    list.bordered_rect(r, radius, gfx::mix(palette_.panel, palette_.raised, lit * 0.5f), 1,
                       gfx::mix(palette_.line, palette_.accent.with_alpha(0.55f), lit));
    // A band of light along the top edge: the panel reads as glass, not as a box.
    list.gradient_rect({r.x + 1, r.y + 1, r.w - 2, std::min(r.h - 2, 64.0f)}, radius,
                       Color::rgb(0xffffff, palette_.day ? 0.35f : 0.035f),
                       Color::rgb(0xffffff, 0.0f));
}

float NativeUI::Impl::chip_width(std::string_view value, bool dot) const
{
    return fonts_.semibold.measure(value, 19) + 32 + (dot ? 18 : 0);
}

float NativeUI::Impl::chip(gfx::DrawList &list, float x, float cy, std::string_view value,
                           Color color, bool dot, bool right_edge) const
{
    const float width = chip_width(value, dot);
    const float left = right_edge ? x - width : x;
    const Rect r{left, cy - 18, width, 36};
    list.bordered_rect(r, 18, gfx::mix(palette_.panel, color, 0.10f), 1, color.with_alpha(0.38f));
    float pen = left + 16;
    if (dot)
    {
        list.circle(pen + 4, cy, 4, color);
        pen += 18;
    }
    ui::text(list, fonts_.semibold, value, pen, cy + 6.5f, 19, palette_.ink);
    return width;
}

void NativeUI::Impl::spinner(gfx::DrawList &list, float cx, float cy, float radius,
                             Color color) const
{
    list.ring(cx, cy, radius, 2, color.with_alpha(0.18f));
    const float sweep = reduced_motion() ? 4.4f : 2.2f + 1.6f * ui::breathe(clock_, 1.6f);
    list.arc(cx, cy, radius, 2, reduced_motion() ? 0 : clock_ * 4.2f, sweep, color);
}

void NativeUI::Impl::thinking_dots(gfx::DrawList &list, float x, float cy, Color color) const
{
    for (int i = 0; i < 3; ++i)
    {
        const float phase = clock_ * 4.0f - static_cast<float>(i) * 0.7f;
        const float wave = reduced_motion() ? 0.6f : 0.5f + 0.5f * std::sin(phase);
        list.circle(x + static_cast<float>(i) * 18 + 6, cy - wave * 5, 5,
                    color.with_alpha(0.35f + 0.65f * wave));
    }
}

std::string NativeUI::Impl::fit(std::string_view value, float size, float width, bool bold) const
{
    return (bold ? fonts_.semibold : fonts_.regular).font->fit(value, size, width);
}

const char *NativeUI::Impl::current_name() const
{
    const auto &state = app_.state();
    return state.selected_model < 0
               ? "No model selected"
               : state.models[static_cast<std::size_t>(state.selected_model)].name.c_str();
}

float NativeUI::Impl::arrival(int order) const
{
    if (reduced_motion())
        return 1;
    const float start = static_cast<float>(order) * 0.055f;
    return tween::cubic_out(std::clamp((page_age_ - start) / 0.42f, 0.0f, 1.0f));
}

std::string NativeUI::Impl::stats_line() const
{
    const auto &state = app_.state();
    if (!state.stats_valid)
        return {};
    const auto &stats = state.stats;
    char line[128];
    const double seconds = static_cast<double>(stats.elapsed_microseconds) / 1000000.0;
    if (state.stats_kind != Capability::Text)
        std::snprintf(line, sizeof(line), "%.1f s", seconds);
    else
    {
        // Speed is measured after the prompt was read, as the first interface did.
        const std::uint64_t decode = stats.elapsed_microseconds > stats.prefill_microseconds
                                         ? stats.elapsed_microseconds - stats.prefill_microseconds
                                         : stats.elapsed_microseconds;
        const unsigned tokens =
            stats.generated_tokens > 1 ? stats.generated_tokens - 1 : stats.generated_tokens;
        const double rate =
            decode ? static_cast<double>(tokens) * 1000000.0 / static_cast<double>(decode) : 0.0;
        const unsigned context =
            stats.prompt_tokens + stats.generated_tokens - (stats.generated_tokens ? 1 : 0);
        std::snprintf(line, sizeof(line), "%u tokens  \xC2\xB7  %.1f tok/s  \xC2\xB7  %u ctx",
                      stats.generated_tokens, rate, context);
    }
    return line;
}

const char *NativeUI::Impl::activity_text() const
{
    switch (app_.activity())
    {
    case Activity::Starting:
        return "Starting";
    case Activity::PreparingModel:
    case Activity::OpeningConversation:
        return "Preparing";
    case Activity::Generating:
        return "Creating";
    case Activity::Saving:
        return "Saving";
    default:
        break;
    }
    const auto &state = app_.state();
    if (state.selected_model < 0)
        return state.models.empty() ? "No models" : "Choose a model";
    return state.ready ? "Ready" : "Needs attention";
}
} // namespace prospero
