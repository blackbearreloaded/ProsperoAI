// ProsperoAI native frontend, built with ps5-homebrew-ui: the frame, input and state.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "native_ui_impl.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#if __has_include("prospero_build.h")
#include "prospero_build.h" // written by tools/build.sh from sce_sys/param.json
#endif
#ifndef PROSPERO_VERSION
#define PROSPERO_VERSION "development"
#define PROSPERO_BUILD_LABEL ""
#endif

namespace prospero
{
namespace
{
constexpr float kBootSeconds = 1.6f; // the opening is never shorter than its chime
constexpr const char *kTabs[] = {"Workspace", "Models", "Settings"};
constexpr const char *kCategories[] = {"Appearance", "Generation", "Sound", "Accessibility",
                                       "About"};
enum DialogAction
{
    kUseModel = 1,
    kDeleteConversation,
    kCloseApp
};
} // namespace

NativeUI::Impl::Impl(NativeUI &owner, App &app, FontSet &font_set, gfx::Renderer &renderer)
    : owner_(owner), app_(app), font_set_(font_set), fonts_(font_set.fonts()), renderer_(renderer),
      palette_(make_palette(app.state().preferences)),
      theme_(make_theme(palette_, app.state().preferences))
{
    tabs_.style.kind = ui::TabKind::underline;
    tabs_.style.track = false;
    tabs_.style.focus_ring = false;
    tabs_.style.on_page = true;
    tabs_.style.text_size = 25;
    tabs_.style.gap = 38;
    tabs_.style.padding = 8;
    tabs_.style.height = 62;
    std::vector<ui::TabItem> tabs;
    tabs_width_ = 0;
    for (int i = 0; i < 3; ++i)
    {
        tabs.push_back({kTabs[i], 0, false, i});
        tabs_width_ += fonts_.semibold.measure(kTabs[i], 25) + 16 + (i ? 38.0f : 0.0f);
    }
    tabs_.set_tabs(std::move(tabs));
    // The row sits in the middle of the header, between the L1 and R1 glyphs.
    tabs_left_ = 960 - tabs_width_ * 0.5f;
    tabs_.set_bounds({tabs_left_, 57, tabs_width_ + 8, 66});
    tabs_.set_focused(false);

    sessions_.style.title_size = 24;
    sessions_.style.subtitle_size = 19;
    sessions_.style.row_height = 86;
    sessions_.style.gap = 8;
    sessions_.style.padding = 20;
    sessions_.style.highlight.kind = ui::HighlightKind::bar;
    sessions_.set_bounds({110, 322, 334, 598});
    sessions_.set_active(false);

    models_.style.columns = 3;
    models_.style.cell_height = 196;
    models_.style.gap_x = 24;
    models_.style.padding = 14;
    models_.style.card.text = ui::CardText::none;
    models_.style.card.art_aspect = 0;
    models_.style.card.focus_scale = 1.025f;
    models_.style.card.lift = 6;
    models_.style.card.glow = false;
    models_.set_bounds({82, 474, 1756, 456});
    models_.content =
        [this](ui::Canvas &canvas, const Rect &cell, const ui::CardItem &, int index, float focus)
    { draw_model(canvas.list, cell, visible_models_[static_cast<std::size_t>(index)], focus); };

    filters_.style.kind = ui::TabKind::segmented;
    filters_.style.width = ui::TabWidth::fill;
    filters_.style.height = 60;
    filters_.style.text_size = 23;
    filters_.style.wrap = true;
    filters_.set_tabs({{"Download", 0, false, 5},
                       {"All", 0, false, 0},
                       {"Text", 0, false, 1},
                       {"Image", 0, false, 2},
                       {"Audio", 0, false, 3},
                       {"Voice", 0, false, 4}});
    filters_.set_active(1);
    filters_.set_bounds({732, 392, 1092, 60});
    filters_.set_focused(false);
    search_.style.max_rows = 0;
    search_.set_bounds({kLeft, 392, 612, 60});
    search_.set_placeholder("Search your models");
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

    form_.style.row_height = 88;
    form_.style.gap = 10;
    form_.style.label_size = 26;
    form_.style.value_size = 24;
    form_.style.control_width = 300;
    form_.style.highlight.kind = ui::HighlightKind::ring;
    form_.style.description_inline = false;
    form_.style.dividers = true;
    form_.style.padding = 26;
    form_.set_bounds({494, 362, 912, 520});

    dialog_.style.width = 830;
    dialog_.style.title_size = 40;
    dialog_.style.body_size = 27;
    dialog_.style.icon_size = 0;
    dialog_.style.padding = 48;
    dialog_.style.scrim = 0.62f;
    dialog_.style.frost = 0.78f;
    dialog_.style.centered = false;

    composer_.set_bounds({536, 850, 1288, 72});
    composer_.style.field_height = 72;
    composer_.style.max_length = 1023;
    composer_.style.counter = false;
    composer_.set_placeholder("Take an idea a little further...");
    chat_.set_bounds({536, 372, 1272, 400});
    chat_.style.clip_bleed = 0;

    categories_.style = sessions_.style;
    categories_.style.row_height = 76;
    categories_.style.title_size = 25;
    categories_.set_bounds({110, 362, 322, 520});
    std::vector<ui::ListItem> categories;
    for (const char *name : kCategories)
    {
        ui::ListItem item;
        item.title = name;
        categories.push_back(std::move(item));
    }
    categories_.set_items(std::move(categories));
    categories_.set_active(false);

    toasts_.style.anchor = ui::ToastAnchor::top_right;
    toasts_.style.margin = 96;
    toasts_.style.width = 500;
    toasts_.set_bounds({0, 78, gfx::kVirtualWidth, 900});

    apply_theme();
    page_top_.snap(palette_.page_top);
    page_bottom_.snap(palette_.page_bottom);
    cloud_a_.snap(palette_.page_bottom);
    cloud_b_.snap(palette_.page_bottom);
    refresh_models();
    build_form();
}

void NativeUI::Impl::apply_theme()
{
    const auto &p = app_.state().preferences;
    palette_ = make_palette(p);
    theme_ = make_theme(palette_, p);
    const std::initializer_list<ui::ComponentStyle *> styles{
        &tabs_.style,   &filters_.style,  &sessions_.style, &categories_.style,
        &models_.style, &search_.style,   &keyboard_.style, &form_.style,
        &dialog_.style, &composer_.style, &chat_.style,     &toasts_.style};
    for (ui::ComponentStyle *style : styles)
    {
        style->theme = theme_;
        style->reduced_motion = p.reduced_motion;
    }
    sessions_.style.highlight.color = categories_.style.highlight.color =
        palette_.accent.with_alpha(0.75f);
}

void NativeUI::Impl::change_page(int page)
{
    tabs_.set_active(page);
    page_age_ = 0;
    hero_age_ = 0;
    models_.enter();
    form_.enter();
    sessions_.enter();
}

void NativeUI::Impl::announce(ui::Feedback &feedback)
{
    for (const Notice notice : app_.take_notices())
    {
        const auto &state = app_.state();
        const bool watching = tabs_.active() == 0 && conversation_;
        switch (notice)
        {
        case Notice::ModelReady:
            pending_model_ = -1;
            toasts_.push(ui::StatusKind::success, std::string(current_name()) + " is ready",
                         "A new conversation starts with this model.");
            break;
        case Notice::ModelFailed:
            pending_model_ = -1;
            toasts_.push(ui::StatusKind::danger, "The model could not be prepared",
                         "Choose it again in Models to retry.", 6);
            break;
        case Notice::ModelMissing:
            toasts_.push(ui::StatusKind::warning, "This model is not installed",
                         "The conversation can be read, but not continued.", 6);
            break;
        case Notice::ReplyReady:
            if (watching)
                feedback.play(audio::Cue::notify, 1.12f, 0.0f, 0.8f);
            else
                toasts_.push(ui::StatusKind::success, "Your answer is ready", state.session.title);
            break;
        case Notice::MediaReady:
            if (watching)
                feedback.play(audio::Cue::complete);
            else
                toasts_.push(ui::StatusKind::success,
                             state.stats_kind == Capability::Image ? "Your image is ready"
                                                                   : "Your audio is ready",
                             state.session.title);
            break;
        case Notice::GenerationFailed:
            toasts_.push(ui::StatusKind::danger, "That did not work", state.status, 6);
            break;
        case Notice::SaveFailed:
            toasts_.push(ui::StatusKind::warning, "Not saved yet", state.status, 6);
            break;
        case Notice::ConversationDeleted:
            toasts_.push(ui::StatusKind::info, "Conversation deleted");
            break;
        case Notice::SettingsNotSaved:
            toasts_.push(ui::StatusKind::warning, "Settings not saved",
                         "Your changes apply until ProsperoAI closes.", 6);
            break;
        case Notice::None:
            break;
        }
    }
}

void NativeUI::Impl::update(const InputFrame &input, float dt, ui::Feedback &feedback)
{
    prospero_model_download::poll();
    if (prospero_model_download::state() != prospero_model_download::State::Loading)
        search_.set_busy(false);
    const bool downloading_models = filters_.active() == 0;
    search_.set_placeholder(downloading_models ? "Search model names (e.g. Mistral 7B)"
                                                  : "Search your models");
    search_.set_bounds(downloading_models ? Rect{128, 558, 1056, 64}
                                              : Rect{kLeft, 392, 612, 60});
    filters_.set_bounds((downloading_models || app_.state().models.empty())
                            ? Rect{kLeft, 392, 1728, 60}
                            : Rect{732, 392, 1092, 60});
    const bool was_generating = app_.generating();
    app_.poll();
    if (!welcomed_)
    {
        welcomed_ = true;
        feedback.play(audio::Cue::welcome);
    }
    announce(feedback);
    // Faces for scripts first seen on the last frame are read here, once.
    font_set_.load_pending();
    if (revision_ != app_.state().revision || font_revision_ != font_set_.revision())
        sync();
    upload_visible_image();

    clock_ += reduced_motion() ? 0 : dt;
    page_age_ += dt;
    hero_age_ += dt;
    boot_age_ += dt;
    if (app_.generating())
        busy_seconds_ = was_generating ? busy_seconds_ + dt : 0;
    for (auto &row : chat_rows_)
        row.age += dt;
    send_pulse_.update(dt, 4);

    // The opening covers the first moments: the catalogue is read behind it.
    const bool booting = !app_.state().initialized || boot_age_ < kBootSeconds;
    const float boot_step = dt / (reduced_motion() ? 0.15f : 0.5f);
    boot_ = std::clamp(boot_ + (booting ? boot_step : -boot_step), 0.0f, 1.0f);

    // The backdrop takes the accent and the colour of what is in focus.
    const auto &state = app_.state();
    Capability hue = Capability::Text;
    if (tabs_.active() == 1 && !visible_models_.empty())
        hue = state
                  .models[static_cast<std::size_t>(
                      visible_models_[static_cast<std::size_t>(models_.focus())])]
                  .capability;
    else if (state.selected_model >= 0)
        hue = state.models[static_cast<std::size_t>(state.selected_model)].capability;
    // A conversation model's own colour is the accent, so the second cloud is a cool
    // tone instead: the page keeps a warm side and a deep side.
    const Color second = hue == Capability::Text ? palette_.depth : kind_color(hue);
    cloud_a_.target(gfx::mix(palette_.page_bottom, palette_.accent, palette_.day ? 0.16f : 0.20f));
    cloud_b_.target(gfx::mix(palette_.page_bottom, second, palette_.day ? 0.30f : 0.24f));
    page_top_.target(palette_.page_top);
    page_bottom_.target(palette_.page_bottom);
    const float colour_speed = reduced_motion() ? 60.0f : 4.0f;
    for (ui::SpringColor *colour : {&cloud_a_, &cloud_b_, &page_top_, &page_bottom_})
        colour->update(dt, colour_speed);

    if (boot_ > 0.6f || keyboard_pending_)
    {
        // Nothing is in reach yet.
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
            if (dialog_action_ == kUseModel)
            {
                accepted = app_.select_model(static_cast<unsigned>(dialog_index_));
                if (accepted)
                    pending_model_ = dialog_index_;
            }
            else if (dialog_action_ == kDeleteConversation)
                accepted = app_.delete_session(static_cast<unsigned>(dialog_index_));
            else if (dialog_action_ == kCloseApp)
                accepted = quit_ = !app_.busy() && !app_.state().unsaved;
            if (!accepted)
                feedback.play(audio::Cue::error);
        }
    }
    else if (input.is_pressed(Action::menu))
    {
        if (app_.busy() || state.unsaved)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.7f);
            toasts_.push(ui::StatusKind::info,
                         app_.busy() ? "Still working" : "This conversation is not saved yet",
                         app_.busy() ? "ProsperoAI can close once this is finished."
                                     : "Retry the save before closing.");
        }
        else
            open_dialog(kCloseApp, -1, "Close ProsperoAI?",
                        "Your conversations stay on this console.", "Close", false, feedback);
    }
    else if (input.is_pressed(Action::page_next) || input.is_pressed(Action::page_prev))
    {
        if (tabs_.step(input.is_pressed(Action::page_next) ? 1 : -1, input, feedback) ==
            ui::Event::changed)
            change_page(tabs_.active());
    }
    else if (tabs_.active() == 1)
        handle_models(input, feedback);
    else if (tabs_.active() == 2)
        handle_settings(input, feedback);
    else
        handle_workspace(input, feedback);

    // The model under the focus leads the Models page; a new one arrives with a short fade.
    const int hero = tabs_.active() == 1 && !visible_models_.empty()
                         ? visible_models_[static_cast<std::size_t>(models_.focus())]
                         : -1;
    if (hero != hero_model_)
    {
        hero_model_ = hero;
        hero_age_ = 0;
    }

    sessions_.set_active(rail_);
    categories_.set_active(category_focus_);
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
    toasts_.update(dt, feedback);
}

void NativeUI::Impl::handle_models(const InputFrame &input, ui::Feedback &feedback)
{
    const auto state = prospero_model_download::state();
    const bool has_download_results = state == prospero_model_download::State::Ready ||
                                      state == prospero_model_download::State::SearchReady;
    const auto open_search = [&]
    {
        search_open_ = true;
        search_.set_active(true);
        keyboard_.style.max_length = filters_.active() == 0 ? 128 : 64;
        keyboard_.set_length(search_.length());
        keyboard_.enter();
        feedback.play(audio::Cue::modal_open);
    };

    if (filters_focus_)
    {
        if (input.is_pressed(Action::north))
        {
            filters_.step(1, input, feedback);
            download_focus_ = 0;
            empty_models_download_focus_ = app_.state().models.empty() && filters_.active() != 0;
            refresh_models();
        }
        else if (input.nav == Direction::left || input.nav == Direction::right)
        {
            if (filters_.handle(input, feedback) == ui::Event::changed)
            {
                download_focus_ = 0;
                empty_models_download_focus_ = app_.state().models.empty() && filters_.active() != 0;
                refresh_models();
            }
        }
        else if (input.nav == Direction::down || input.is_pressed(Action::confirm))
        {
            filters_focus_ = false;
            filters_.set_focused(false);
            empty_models_download_focus_ = app_.state().models.empty() && filters_.active() != 0;
        }
        else if (input.is_pressed(Action::west))
            open_search();
        return;
    }

    if (filters_.active() == 0)
    {
        if (input.nav == Direction::up && (!has_download_results || download_focus_ == 0))
        {
            filters_focus_ = true;
            filters_.set_focused(true);
            return;
        }
        if (input.is_pressed(Action::west))
            open_search();
        else if (input.nav == Direction::up && has_download_results)
            download_focus_ = std::max(0, download_focus_ - 1);
        else if (input.nav == Direction::down && has_download_results)
            download_focus_ = std::min(static_cast<int>(prospero_model_download::candidate_count()) - 1,
                                       download_focus_ + 1);
        else if (input.is_pressed(Action::confirm))
        {
            if (state == prospero_model_download::State::Ready)
            {
                if (!prospero_model_download::download(static_cast<std::size_t>(download_focus_)))
                    feedback.play(audio::Cue::error);
            }
            else if (state == prospero_model_download::State::SearchReady)
            {
                prospero_model_download::Candidate candidate{};
                if (prospero_model_download::candidate(static_cast<std::size_t>(download_focus_),
                                                       &candidate) &&
                    prospero_model_download::browse(candidate.name))
                    search_.set_busy(true);
                else
                    feedback.play(audio::Cue::error);
            }
            else if (state != prospero_model_download::State::Loading &&
                     state != prospero_model_download::State::Searching &&
                     state != prospero_model_download::State::Downloading)
            {
                if (prospero_model_download::search(search_.text().c_str()))
                {
                    download_focus_ = 0;
                    search_.set_busy(true);
                }
                else
                    feedback.play(audio::Cue::error);
            }
        }
        return;
    }
    if (app_.state().models.empty())
    {
        if (input.nav == Direction::up && empty_models_download_focus_)
        {
            empty_models_download_focus_ = false;
            filters_focus_ = true;
            filters_.set_focused(true);
        }
        else if (input.nav == Direction::down)
            empty_models_download_focus_ = true;
        else if (input.is_pressed(Action::confirm) && empty_models_download_focus_)
        {
            filters_.set_active(0);
            filters_focus_ = false;
            filters_.set_focused(false);
            feedback.play(audio::Cue::tab);
        }
        else if (input.is_pressed(Action::west))
            open_search();
    }
    else if (input.nav == Direction::up)
    {
        filters_focus_ = true;
        filters_.set_focused(true);
    }
    else if (input.is_pressed(Action::west))
        open_search();
    else if (models_.handle(input, feedback) == ui::Event::activated && !visible_models_.empty())
    {
        const int index = visible_models_[static_cast<std::size_t>(models_.focus())];
        const auto &state = app_.state();
        if (app_.busy() || state.unsaved)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.7f);
            toasts_.push(ui::StatusKind::info,
                         app_.busy() ? "Still working" : "This conversation is not saved yet",
                         app_.busy() ? "Choose a model once this is finished."
                                     : "Retry the save in Workspace first.");
        }
        else
            open_dialog(kUseModel, index,
                        "Use " + state.models[static_cast<std::size_t>(index)].name + "?",
                        "Your current conversation is kept. A new conversation starts with "
                        "this model.",
                        "Use model", false, feedback);
    }
}

void NativeUI::Impl::handle_settings(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back) && !category_focus_)
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
}

void NativeUI::Impl::handle_workspace(const InputFrame &input, ui::Feedback &feedback)
{
    chat_.handle(input, feedback);
    if (input.is_pressed(Action::west))
    {
        if (app_.new_session())
        {
            conversation_ = true;
            rail_ = false;
            composer_.clear();
            page_age_ = 0;
            feedback.play(audio::Cue::open);
        }
        else
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.7f);
    }
    else if (!conversation_)
    {
        if (input.is_pressed(Action::confirm))
            submit(feedback);
        else if (input.nav == Direction::left && !app_.state().sessions.empty())
        {
            conversation_ = rail_ = true;
            page_age_ = 0;
            feedback.play(audio::Cue::open);
        }
    }
    else if (input.nav == Direction::left && !rail_)
    {
        rail_ = true;
        feedback.play(audio::Cue::focus, 0.94f, -0.4f);
    }
    else if (rail_ && (input.nav == Direction::right || input.is_pressed(Action::back)))
    {
        rail_ = false;
        feedback.play(audio::Cue::focus, 1.0f, 0.2f);
    }
    else if (rail_)
    {
        if (input.is_pressed(Action::north) && sessions_.focus() > 0)
        {
            if (app_.busy())
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.7f);
            else
                open_dialog(kDeleteConversation, sessions_.focus() - 1, "Delete this conversation?",
                            "The conversation and its saved media will be removed from this "
                            "console.",
                            "Delete", true, feedback);
        }
        else if (sessions_.handle(input, feedback) == ui::Event::activated)
        {
            const bool accepted =
                sessions_.focus() == 0
                    ? app_.new_session()
                    : app_.open_session(static_cast<unsigned>(sessions_.focus() - 1));
            if (accepted)
            {
                rail_ = false;
                composer_.clear();
                page_age_ = 0;
            }
            else
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.7f);
        }
    }
    else if (input.is_pressed(Action::back) && app_.state().messages.empty() && !app_.busy())
    {
        // An empty conversation steps back to the opening page.
        conversation_ = false;
        page_age_ = 0;
        feedback.play(audio::Cue::back);
    }
    else if (input.is_pressed(Action::confirm))
        submit(feedback);
    else if (input.is_pressed(Action::north))
    {
        const bool accepted = app_.state().retry_available ? app_.retry() : app_.play_audio();
        feedback.play(accepted ? audio::Cue::select : audio::Cue::error, 1.0f, 0.0f,
                      accepted ? 1.0f : 0.7f);
    }
    else if (input.nav == Direction::up)
        chat_.scroll_by(0, -140);
    else if (input.nav == Direction::down)
        chat_.scroll_by(0, 140);
}

void NativeUI::Impl::open_dialog(int action, int index, std::string title, std::string body,
                                 const char *button, bool destructive, ui::Feedback &feedback)
{
    dialog_action_ = action;
    dialog_index_ = index;
    ui::DialogContent content;
    content.title = std::move(title);
    content.body = std::move(body);
    content.buttons = {{"Not now", ui::ButtonKind::secondary, false},
                       {button, ui::ButtonKind::primary, destructive}};
    dialog_.open(std::move(content), feedback);
}

void NativeUI::Impl::refresh_models()
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
        const int selected_filter = filters_.active() == 0 ? 0 : filters_.active() - 1;
        if ((selected_filter == 0 || selected_filter == capability) &&
            name.find(query) != std::string::npos)
            visible_models_.push_back(static_cast<int>(i));
    }
    models_.set_count(static_cast<int>(visible_models_.size()));
    models_.set_focus(0);
    models_.enter();
}

void NativeUI::Impl::type(char character, ui::Feedback &feedback)
{
    if (dialog_.is_open() || keyboard_pending_ || boot_ > 0.6f)
        return;
    if (tabs_.active() == 1)
    {
        search_.insert(character, feedback);
        refresh_models();
    }
    else if (tabs_.active() == 0 && !app_.busy())
    {
        if (!conversation_)
            page_age_ = 0;
        conversation_ = true;
        rail_ = false;
        composer_.insert(character, feedback);
    }
}

void NativeUI::Impl::backspace(ui::Feedback &feedback)
{
    if (dialog_.is_open() || keyboard_pending_ || boot_ > 0.6f)
        return;
    if (tabs_.active() == 1)
    {
        search_.backspace(feedback);
        refresh_models();
    }
    else if (tabs_.active() == 0 && !app_.busy())
        composer_.backspace(feedback);
}

void NativeUI::Impl::submit(ui::Feedback &feedback)
{
    if (search_open_)
    {
        search_open_ = false;
        search_.set_active(false);
        feedback.play(audio::Cue::modal_close);
        return;
    }
    if (tabs_.active() != 0 || dialog_.is_open() || keyboard_pending_ || boot_ > 0.6f)
        return;
    if (app_.busy())
    {
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        return;
    }
    if (!app_.can_send())
    {
        // Nothing can answer yet: the library is where that is put right.
        change_page(1);
        feedback.play(audio::Cue::tab);
        toasts_.push(ui::StatusKind::info,
                     app_.state().models.empty() ? "No models installed" : "Choose a model first",
                     app_.state().models.empty()
                         ? "Copy a model folder into the app's models folder."
                         : "This conversation's model is not ready.");
        return;
    }
    if (!conversation_)
        page_age_ = 0;
    conversation_ = true;
    rail_ = false;
    if (!composer_.text().empty())
    {
        if (app_.send(composer_.text()))
        {
            composer_.clear();
            send_pulse_.trigger();
            feedback.play(audio::Cue::select);
            chat_.scroll_to(0, chat_.max_y());
        }
        else
            feedback.play(audio::Cue::error);
    }
    else if (owner_.request_keyboard)
    {
        keyboard_pending_ = true;
        feedback.play(audio::Cue::open);
        owner_.request_keyboard(composer_.text());
    }
}

void NativeUI::Impl::keyboard_result(const char *value)
{
    keyboard_pending_ = false;
    if (value && *value)
    {
        composer_.set_text(value);
        // The OS keyboard's Done action sends, matching the original app.
        if (app_.send(composer_.text()))
        {
            composer_.clear();
            send_pulse_.trigger();
        }
    }
}

void NativeUI::Impl::build_form()
{
    const auto &p = app_.state().preferences;
    form_.clear();
    if (category_ == 0)
    {
        form_.add_choice(1, "Color theme", {"Midnight", "Daylight"}, static_cast<int>(p.theme));
        form_.add_choice(2, "Accent color", {"Amber", "Mist", "Sage"}, static_cast<int>(p.accent));
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
        form_.add_slider(3, "Interface sounds", static_cast<float>(p.volume), 0, 100, 5).unit = "%";
    else if (category_ == 3)
    {
        form_.add_toggle(4, "Reduce motion", p.reduced_motion);
        form_.add_toggle(5, "High contrast", p.high_contrast);
        form_.add_choice(8, "Reading size", {"Standard", "Large"}, static_cast<int>(p.text_size));
    }
    else
    {
        const std::string label = PROSPERO_BUILD_LABEL;
        form_.add_value(10, "Version",
                        label.empty() ? PROSPERO_VERSION : PROSPERO_VERSION "  \xC2\xB7  " + label);
        form_.add_value(11, "Made by", "BlackBearReloaded");
        form_.add_value(12, "Interface", "ps5-homebrew-ui");
        form_.add_value(13, "Graphics", "ps5-opengl");
        form_.add_value(14, "License", "GPL-3.0-or-later");
    }
    form_.enter();
}

void NativeUI::Impl::apply_form()
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
    else if (category_ == 3)
    {
        p.reduced_motion = form_.toggle_value(4);
        p.high_contrast = form_.toggle_value(5);
        p.text_size = static_cast<unsigned>(form_.choice_index(8));
    }
    app_.set_preferences(std::move(p));
    apply_theme();
}

void NativeUI::Impl::sync()
{
    const auto &state = app_.state();
    revision_ = state.revision;
    const bool faces_changed = font_revision_ != font_set_.revision();
    font_revision_ = font_set_.revision();
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
        for (const auto &model : state.models)
            font_set_.note(model.name);
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
        first.title = "New conversation";
        first.subtitle = "Start fresh";
        rows.push_back(std::move(first));
        for (const auto &session : state.sessions)
        {
            ui::ListItem item;
            item.title = session.title;
            item.subtitle = session.model_name;
            font_set_.note(item.title);
            rows.push_back(std::move(item));
        }
        sessions_.set_items(std::move(rows));
        sessions_.set_focus(focus);
    }
    font_set_.note(composer_.text());

    const bool follow = chat_.max_y() - chat_.offset_y() < 80;
    const float size = state.preferences.text_size ? 34.0f : 28.0f;
    const bool remeasure = body_size_ != size || faces_changed;
    body_size_ = size;
    const float line = body_size_ * 1.48f;
    // While an answer is on its way it has a row of its own, empty until the first words.
    const bool streaming = app_.generating();
    const std::size_t previous = chat_rows_.size();
    chat_rows_.resize(state.messages.size() + (streaming ? 1 : 0));
    float y = 0;
    for (std::size_t i = 0; i < chat_rows_.size(); ++i)
    {
        auto &row = chat_rows_[i];
        const bool stored = i < state.messages.size();
        const char *value = stored ? state.messages[i].content : state.stream.c_str();
        const char *role = stored ? state.messages[i].role : "assistant";
        if (i >= previous)
            row.age = 0;
        if (remeasure || row.text != value || row.role != role)
        {
            row.text = value;
            row.role = role;
            row.image_path.clear();
            row.audio = false;
            std::string visible = row.text;
            const bool user = row.role == "user";
            if (!user)
            {
                const auto start = visible.find("/download0/");
                const auto end = start == std::string::npos ? start : visible.find(".tga", start);
                if (end != std::string::npos)
                    row.image_path = visible.substr(start, end + 4 - start);
                row.audio =
                    start != std::string::npos && visible.find(".wav", start) != std::string::npos;
                if (!row.image_path.empty() || row.audio)
                    visible.resize(start);
                while (!visible.empty() && (visible.back() == '\n' || visible.back() == '\r'))
                    visible.pop_back();
            }
            font_set_.note(visible);
            row.lines = fonts_.regular.font->wrap(visible, body_size_, user ? 900.0f : 1240.0f);
            row.width = 0;
            for (const auto &text_line : row.lines)
                row.width = std::max(row.width, fonts_.regular.measure(text_line, body_size_));
            const float lines = static_cast<float>(std::max<std::size_t>(row.lines.size(), 1));
            row.height = user ? lines * line + 34 + 28 : 58 + lines * line + 30;
            if (!row.image_path.empty())
                row.height += 382;
            else if (row.audio)
                row.height += 112;
        }
        row.y = y;
        y += row.height;
    }
    chat_.set_content_size(chat_.bounds().w, y);
    if (follow)
        chat_.scroll_to(0, chat_.max_y());
    if (!state.messages.empty() && !conversation_)
    {
        conversation_ = true;
        page_age_ = 0;
    }
}

void NativeUI::Impl::upload_visible_image()
{
    if (tabs_.active() != 0 || !conversation_)
        return;
    for (const auto &row : chat_rows_)
    {
        if (row.image_path.empty() || images_.count(row.image_path) ||
            row.y + row.height < chat_.offset_y() || row.y > chat_.offset_y() + chat_.bounds().h)
            continue;
        for (const auto &image : app_.state().images)
        {
            if (image->path != row.image_path)
                continue;
            const auto texture = renderer_.batch().create_texture(static_cast<int>(image->width),
                                                                  static_cast<int>(image->height),
                                                                  image->rgba.data());
            if (texture)
                images_.emplace(image->path,
                                Texture{texture, static_cast<float>(image->width) /
                                                     static_cast<float>(image->height)});
            return; // At most one bounded upload per frame.
        }
    }
}

void NativeUI::Impl::release()
{
    for (const auto &[path, texture] : images_)
        glDeleteTextures(1, &texture.id);
    images_.clear();
}

void NativeUI::Impl::draw(UiFrame &frame) const
{
    const auto &preferences = app_.state().preferences;
    if (preferences.high_contrast)
    {
        // Nothing moves or glows behind the text.
        frame.backdrop.mode = gfx::BackdropMode::gradient;
        frame.backdrop.colors[0] = palette_.page_top;
        frame.backdrop.colors[1] = palette_.page_bottom;
    }
    else if (palette_.day)
    {
        // Daylight: paper under a slow light. The clouds' darkened corners would
        // only grey a pale page.
        frame.backdrop.mode = gfx::BackdropMode::gradient;
        frame.backdrop.colors[0] = page_top_.value();
        frame.backdrop.colors[1] = page_bottom_.value();
        frame.backdrop.colors[2] =
            gfx::mix(cloud_a_.value(), cloud_b_.value(), 0.5f + 0.5f * std::sin(clock_ * 0.11f));
        frame.backdrop.params[0] = 0.72f + 0.10f * std::sin(clock_ * 0.07f);
        frame.backdrop.params[1] = 0.30f + 0.08f * std::cos(clock_ * 0.05f);
        frame.backdrop.params[2] = 0.16f;
    }
    else
    {
        frame.backdrop.mode = gfx::BackdropMode::aurora;
        frame.backdrop.colors[0] = page_top_.value();
        frame.backdrop.colors[1] = page_bottom_.value();
        frame.backdrop.colors[2] = cloud_a_.value();
        frame.backdrop.colors[3] = cloud_b_.value();
        frame.backdrop.time = 40 + clock_;
    }
    auto &list = frame.scene;
    ui::Canvas canvas{list, fonts_, 0, clock_};
    draw_header(canvas);
    const float entrance = arrival(0);
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
    if (boot_ > 0.001f)
        draw_boot(list);

    ui::Canvas overlay{frame.overlay, fonts_, 0, clock_};
    if (search_open_)
        draw_search(overlay);
    if (dialog_.visible())
    {
        frame.glass = true;
        ui::Canvas frosted{frame.overlay, fonts_, frame.glass_texture, clock_};
        dialog_.draw(frosted);
    }
    toasts_.draw(overlay);
}

void NativeUI::Impl::draw_header(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    draw_mark(list, 119, 91, 1.05f, palette_.accent);
    text(list, "ProsperoAI", 159, 101, 29, palette_.ink, true);

    const auto glyphs = palette_.day ? ui::GlyphStyle::light() : ui::GlyphStyle::dark();
    const float shoulder = ui::button_width(ui::Button::l1, 30);
    ui::draw_button(list, fonts_, glyphs, ui::Button::l1, tabs_left_ - shoulder - 26, 90, 30);
    tabs_.draw(canvas);
    ui::draw_button(list, fonts_, glyphs, ui::Button::r1, tabs_left_ + tabs_width_ + 30, 90, 30);

    // What can answer, and what it is doing.
    const auto &state = app_.state();
    const bool busy = app_.busy();
    const Color tone = busy                                             ? palette_.warn
                       : state.selected_model >= 0 && state.ready       ? palette_.good
                       : state.selected_model < 0 && !state.initialized ? palette_.muted
                                                                        : palette_.bad;
    std::string status = state.selected_model >= 0 ? fit(current_name(), 19, 250) : std::string();
    if (!status.empty())
        status += "  \xC2\xB7  ";
    status += activity_text();
    const float width = chip_width(status, true);
    chip(list, kRight, 90, status, tone, true, true);
    if (busy && !reduced_motion())
        list.ring(kRight - width + 20, 90, 4 + 7 * ui::breathe(clock_, 1.2f), 1.5f,
                  tone.with_alpha(0.5f * (1 - ui::breathe(clock_, 1.2f))));
    list.line(kLeft, kHeaderRule, kRight, kHeaderRule, 1, palette_.line);
}

void NativeUI::Impl::draw_footer(gfx::DrawList &list) const
{
    list.line(kLeft, kFooterRule, kRight, kFooterRule, 1, palette_.line);
    const auto &state = app_.state();
    ui::Hint hints[6];
    int count = 0;
    if (dialog_.is_open())
    {
        hints[count++] = {ui::Button::cross, "Choose"};
        hints[count++] = {ui::Button::circle, "Back"};
    }
    else if (search_open_)
    {
        hints[count++] = {ui::Button::cross, "Type"};
        hints[count++] = {ui::Button::circle, "Done"};
    }
    else if (tabs_.active() == 1)
    {
        if (!visible_models_.empty())
            hints[count++] = {ui::Button::cross, "Use model"};
        if (!state.models.empty())
        {
            hints[count++] = {ui::Button::triangle, "Filter"};
            hints[count++] = {ui::Button::square, "Search"};
        }
    }
    else if (tabs_.active() == 2)
    {
        hints[count++] = {ui::Button::cross, category_focus_ ? "Open" : "Adjust"};
        if (!category_focus_)
            hints[count++] = {ui::Button::circle, "Categories"};
    }
    else if (!conversation_)
    {
        hints[count++] = {ui::Button::cross, app_.can_send() ? "Start" : "Models"};
        if (!state.sessions.empty())
            hints[count++] = {ui::Button::dpad, "Conversations"};
    }
    else if (rail_)
    {
        hints[count++] = {ui::Button::cross, "Open"};
        if (sessions_.focus() > 0)
            hints[count++] = {ui::Button::triangle, "Delete"};
        hints[count++] = {ui::Button::circle, "Back"};
        hints[count++] = {ui::Button::square, "New"};
    }
    else
    {
        const bool audio = std::any_of(chat_rows_.begin(), chat_rows_.end(),
                                       [](const auto &r) { return r.audio; });
        hints[count++] = {ui::Button::cross, composer_.text().empty() ? "Write" : "Send"};
        if (state.retry_available)
            hints[count++] = {ui::Button::triangle, "Retry"};
        else if (audio)
            hints[count++] = {ui::Button::triangle, "Play audio"};
        hints[count++] = {ui::Button::square, "New"};
        hints[count++] = {ui::Button::right_stick, "Scroll"};
    }
    if (!dialog_.is_open() && !search_open_)
        hints[count++] = {ui::Button::options, "Close"};
    auto glyphs = palette_.day ? ui::GlyphStyle::light() : ui::GlyphStyle::dark();
    glyphs.label = palette_.ink;
    ui::HintLayout layout;
    layout.size = 32;
    layout.text_size = 22;
    layout.cy = 1012;
    layout.item_gap = 34;
    ui::draw_hints(list, fonts_, glyphs, hints, count, kRight, true, layout);
    label(list, "PRIVATE BY DESIGN", kLeft, 1019, palette_.muted);
}

void NativeUI::Impl::draw_boot(gfx::DrawList &list) const
{
    const float veil = reduced_motion() ? boot_ : tween::cubic_out(boot_);
    list.push_opacity(veil);
    list.gradient_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0, palette_.page_top,
                       palette_.page_bottom);
    // The doors part to reveal the warm light beyond the ProsperoAI arch.
    const float opening = reduced_motion()
                              ? 1.0f
                              : tween::cubic_out(std::clamp((boot_age_ - 0.12f) / 0.95f, 0.0f,
                                                            1.0f));
    const float lift = (1 - veil) * -24;
    constexpr float center = 960.0f;
    constexpr float floor = 742.0f;
    const float side = 220.0f;
    const float arch_y = 438.0f + lift;
    const float arch_rx = 220.0f;
    const float arch_ry = 214.0f;
    const Color gold = palette_.accent;
    list.glow({center - (10 + 120 * opening), arch_y - 110, 20 + 240 * opening, 350}, 30,
              145, gold.with_alpha(0.08f + 0.13f * opening));

    // Nested arch strokes stay fixed while the two inset door leaves slide outward.
    for (int ring = 0; ring < 3; ++ring)
    {
        const float inset = static_cast<float>(ring) * 18.0f;
        const float rx = arch_rx - inset;
        const float ry = arch_ry - inset;
        float last_x = center - rx;
        float last_y = arch_y;
        for (int step = 1; step <= 32; ++step)
        {
            const float angle = 3.14159265f + 3.14159265f * static_cast<float>(step) / 32.0f;
            const float x = center + std::cos(angle) * rx;
            const float y = arch_y + std::sin(angle) * ry;
            list.line(last_x, last_y, x, y, ring == 0 ? 3.0f : 1.4f,
                      gold.with_alpha((ring == 0 ? 0.82f : 0.38f) * veil));
            last_x = x;
            last_y = y;
        }
        list.line(center - rx, arch_y, center - rx, floor, ring == 0 ? 3.0f : 1.4f,
                  gold.with_alpha((ring == 0 ? 0.82f : 0.38f) * veil));
        list.line(center + rx, arch_y, center + rx, floor, ring == 0 ? 3.0f : 1.4f,
                  gold.with_alpha((ring == 0 ? 0.82f : 0.38f) * veil));
    }

    const float left_outer = center - side;
    const float right_outer = center + side;
    const float left_top = center - 10 - 140 * opening;
    const float left_bottom = center - 10 - 122 * opening;
    const float right_top = center + 10 + 140 * opening;
    const float right_bottom = center + 10 + 122 * opening;
    const float door_top = arch_y;
    const Color door = gfx::mix(palette_.page_top, palette_.depth, 0.38f).with_alpha(0.92f * veil);
    const float left_leaf[] = {left_outer, door_top, left_top, door_top + 12,
                               left_bottom, floor - 8, left_outer, floor};
    const float right_leaf[] = {right_top, door_top + 12, right_outer, door_top,
                                right_outer, floor, right_bottom, floor - 8};
    list.polygon(left_leaf, 4, door);
    list.polygon(right_leaf, 4, door);
    list.line(left_outer, door_top, left_top, door_top + 12, 2.0f, gold.with_alpha(0.82f * veil));
    list.line(left_top, door_top + 12, left_bottom, floor - 8, 2.0f,
              gold.with_alpha(0.82f * veil));
    list.line(left_bottom, floor - 8, left_outer, floor, 2.0f, gold.with_alpha(0.82f * veil));
    list.line(right_top, door_top + 12, right_outer, door_top, 2.0f,
              gold.with_alpha(0.82f * veil));
    list.line(right_outer, door_top, right_outer, floor, 2.0f, gold.with_alpha(0.82f * veil));
    list.line(right_outer, floor, right_bottom, floor - 8, 2.0f,
              gold.with_alpha(0.82f * veil));
    const float seam = 1 - opening;
    if (seam > 0.01f)
        list.glow({center - 3, door_top + 70, 6, 190}, 3, 38,
                  gold.with_alpha(0.20f * seam * veil));
    list.circle(left_top + 16, 555 + lift, 3.2f, gold.with_alpha(0.8f * veil));
    list.circle(right_top - 16, 555 + lift, 3.2f, gold.with_alpha(0.8f * veil));

    const float words =
        reduced_motion() ? 1 : tween::cubic_out(std::clamp((boot_age_ - 0.25f) / 0.6f, 0.0f, 1.0f));
    list.push_opacity(words);
    ui::text(list, fonts_.display, "ProsperoAI", 960, 690 + (1 - words) * 14 + lift, 64,
             palette_.ink, gfx::Align::center);
    label(list, "RUNS ON YOUR CONSOLE.  STAYS ON YOUR CONSOLE.", 960, 742 + lift, palette_.accent,
          gfx::Align::center);
    const auto &state = app_.state();
    text(list, fit(state.status, 23, 900, false), 960, 852, 23, palette_.muted, false,
         gfx::Align::center);
    // A light travels along the rule while the catalogue is read.
    const Rect track{760, 884, 400, 3};
    list.rounded_rect(track, 1.5f, palette_.line);
    const float travel = reduced_motion() ? 0.5f : std::fmod(boot_age_ * 0.9f, 1.0f);
    list.push_clip(track);
    list.rounded_rect({track.x - 120 + travel * (track.w + 120), track.y, 120, 3}, 1.5f,
                      palette_.accent);
    list.pop_clip();
    text(list, PROSPERO_VERSION, 960, 1012, 19, palette_.muted.with_alpha(0.7f), false,
         gfx::Align::center);
    list.pop_opacity();
    list.pop_opacity();
}

void NativeUI::Impl::draw_search(ui::Canvas &canvas) const
{
    auto &overlay = canvas.list;
    overlay.rounded_rect({0, 0, gfx::kVirtualWidth, gfx::kVirtualHeight}, 0,
                         Color::rgb(0x000000, palette_.day ? 0.45f : 0.72f));
    const Rect sheet{324, 440, 1272, 500};
    overlay.shadow({sheet.x, sheet.y + 18, sheet.w, sheet.h}, 26, 60, Color::rgb(0x000000, 0.45f));
    overlay.bordered_rect(sheet, 26, palette_.panel.with_alpha(1.0f / palette_.panel.a), 1,
                          palette_.line);
    label(overlay, "SEARCH YOUR MODELS", 364, 492, palette_.accent);
    const bool empty = search_.text().empty();
    text(overlay, empty ? "Type a name..." : fit(search_.text(), 32, 900).c_str(), 364, 548, 32,
         empty ? palette_.muted : palette_.ink, !empty);
    char found[64];
    std::snprintf(found, sizeof(found), "%zu found", visible_models_.size());
    text(overlay, found, sheet.x + sheet.w - 40, 548, 22, palette_.muted, false, gfx::Align::right);
    keyboard_.draw(canvas);
}

NativeUI::NativeUI(App &app, FontSet &fonts, gfx::Renderer &renderer)
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
