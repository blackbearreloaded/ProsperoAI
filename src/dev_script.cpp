// ProsperoAI - A scripted, self-ending run for tests on a console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dev_script.hpp"
#include "core/save_file.hpp"
#include "platform/ps5/system.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

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
constexpr float kSlowFrame = 0.020f; // a frame that missed 60 per second by a margin

struct Key
{
    const char *name;
    hui::Action action;
    hui::Direction direction;
};
constexpr Key kKeys[] = {
    {"cross", hui::Action::confirm, hui::Direction::none},
    {"circle", hui::Action::back, hui::Direction::none},
    {"square", hui::Action::west, hui::Direction::none},
    {"triangle", hui::Action::north, hui::Direction::none},
    {"options", hui::Action::menu, hui::Direction::none},
    {"l1", hui::Action::page_prev, hui::Direction::none},
    {"r1", hui::Action::page_next, hui::Direction::none},
    {"up", hui::Action::up, hui::Direction::up},
    {"down", hui::Action::down, hui::Direction::down},
    {"left", hui::Action::left, hui::Direction::left},
    {"right", hui::Action::right, hui::Direction::right},
};

const char *activity_name(Activity activity)
{
    switch (activity)
    {
    case Activity::Starting:
        return "starting";
    case Activity::PreparingModel:
        return "preparing-model";
    case Activity::OpeningConversation:
        return "opening-conversation";
    case Activity::Generating:
        return "generating";
    case Activity::Saving:
        return "saving";
    default:
        return "idle";
    }
}
} // namespace

bool DevScript::load(const std::string &request, const std::string &output)
{
    std::string text;
    if (!hui::save::read_file(request, &text, 16u << 10) || text.empty())
        return false;
    std::string token;
    std::size_t begin = 0;
    while (begin < text.size())
    {
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(begin, end - begin);
        begin = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        const std::size_t space = line.find(' ');
        Step step;
        step.verb = line.substr(0, space);
        step.argument = space == std::string::npos ? std::string() : line.substr(space + 1);
        // The last word of a step may be a number of seconds, pixels or presses.
        const std::size_t last = step.argument.find_last_of(' ');
        step.number = static_cast<float>(
            std::atof(step.argument.c_str() + (last == std::string::npos ? 0 : last + 1)));
        if (step.verb == "token")
            token = step.argument;
        else if (step.verb == "limit")
            limit_ = std::max(step.number, 10.0f);
        else
            steps_.push_back(std::move(step));
    }
    if (token.empty() || steps_.empty())
    {
        steps_.clear();
        return false;
    }
    // A request is played once: the folder it sits in is read-only for the app, so
    // the token of the last one played is remembered beside the report.
    output_ = output;
    const std::size_t parent = output.find_last_of('/');
    if (parent != std::string::npos && parent > 0)
        mkdir(output.substr(0, parent).c_str(), 0755);
    mkdir(output.c_str(), 0755);
    const std::string handled_path = output + "/handled-token.txt";
    std::string handled;
    if (hui::save::read_file(handled_path, &handled, 256) && handled == token)
    {
        steps_.clear();
        return false;
    }
    if (std::FILE *file = std::fopen(handled_path.c_str(), "wb"))
    {
        std::fwrite(token.data(), 1, token.size(), file);
        std::fclose(file);
    }
    std::remove((output + "/report.txt").c_str());
    loaded_ = true;
    report("ProsperoAI scripted run  token=%s  version=%s  build=%s  steps=%zu  limit=%.0f s",
           token.c_str(), PROSPERO_VERSION, PROSPERO_BUILD_LABEL[0] ? PROSPERO_BUILD_LABEL : "-",
           steps_.size(), static_cast<double>(limit_));
    return true;
}

void DevScript::report(const char *format, ...)
{
    char line[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    hui::sys::log("[prosperoai][script] %s", line);
    // Opened and closed per line: the run may end in a way nobody planned.
    if (std::FILE *file = std::fopen((output_ + "/report.txt").c_str(), "ab"))
    {
        std::fprintf(file, "%7.2f  %s\n", static_cast<double>(clock_), line);
        std::fclose(file);
    }
}

void DevScript::status(App &app)
{
    const State &state = app.state();
    const char *model =
        state.selected_model >= 0
            ? state.models[static_cast<std::size_t>(state.selected_model)].id.c_str()
            : "-";
    report("status: models=%zu selected=%s ready=%d activity=%s sessions=%zu messages=%zu "
           "images=%zu retry=%d unsaved=%d text=\"%s\"",
           state.models.size(), model, state.ready ? 1 : 0, activity_name(app.activity()),
           state.sessions.size(), state.messages.size(), state.images.size(),
           state.retry_available ? 1 : 0, state.unsaved ? 1 : 0, state.status.c_str());
    if (state.stats_valid)
        report("answer: tokens=%u prompt_tokens=%u prefill_ms=%llu elapsed_ms=%llu",
               state.stats.generated_tokens, state.stats.prompt_tokens,
               static_cast<unsigned long long>(state.stats.prefill_microseconds / 1000),
               static_cast<unsigned long long>(state.stats.elapsed_microseconds / 1000));
    if (!state.messages.empty())
    {
        // The start of the last message, on one line.
        std::string last(state.messages.back().content, 0, 160);
        std::replace(last.begin(), last.end(), '\n', ' ');
        report("last message (%s): %s", state.messages.back().role, last.c_str());
    }
    if (frames_)
        report("frames: %u in %.1f s, average %.2f ms, worst %.1f ms, %u over %.0f ms", frames_,
               static_cast<double>(frame_sum_), static_cast<double>(frame_sum_ * 1000 / frames_),
               static_cast<double>(frame_worst_ * 1000), slow_,
               static_cast<double>(kSlowFrame * 1000));
    frames_ = slow_ = 0;
    frame_sum_ = frame_worst_ = 0;
}

void DevScript::fail(const char *why)
{
    failed_ = true;
    report("FAILED at step %zu (%s %s): %s", at_ + 1, steps_[at_].verb.c_str(),
           steps_[at_].argument.c_str(), why);
    // Nothing more is tried: skip to the closing step, or to the end.
    while (at_ < steps_.size() && steps_[at_].verb != "quit")
        ++at_;
    entered_ = false;
    step_clock_ = 0;
}

void DevScript::capture_done(bool saved)
{
    report("picture %s %s", capture_.c_str(), saved ? "saved" : "NOT saved");
    capture_.clear();
}

void DevScript::update(float seconds, App &app, NativeUI &ui, hui::InputFrame &input,
                       hui::ui::Feedback &feedback)
{
    if (!active())
        return;
    clock_ += seconds;
    step_clock_ += seconds;
    ++frames_;
    frame_sum_ += seconds;
    frame_worst_ = std::max(frame_worst_, seconds);
    slow_ += seconds > kSlowFrame ? 1 : 0;
    if (!capture_.empty())
        return; // the frame with the picture has not been drawn yet

    if (clock_ > limit_ && !failed_ && at_ < steps_.size() && steps_[at_].verb != "quit")
        fail("the run's time limit passed");
    if (at_ >= steps_.size())
    {
        // A script without a closing step ends as "quit 30" would.
        steps_.push_back({"quit", "30", 30.0f});
    }
    const Step &step = steps_[at_];
    const State &state = app.state();
    bool done = false;
    if (step.verb == "wait")
        done = step_clock_ >= step.number;
    else if (step.verb == "until")
    {
        const bool started = state.initialized;
        const bool idle = started && !app.busy();
        const bool wanted = step.argument.rfind("started", 0) == 0 ? started
                            : step.argument.rfind("ready", 0) == 0 ? idle && state.ready
                                                                   : idle;
        const float patience = step.number > 0 ? step.number : 120.0f;
        if (wanted)
        {
            report("%s %s: after %.1f s", step.verb.c_str(), step.argument.c_str(),
                   static_cast<double>(step_clock_));
            done = true;
        }
        else if (step_clock_ > patience)
        {
            status(app);
            fail("waited too long");
            return;
        }
        else if (idle && step.argument.rfind("ready", 0) == 0)
        {
            // Nothing is running and the model is not ready: it will not become so.
            stalled_ += seconds;
            if (stalled_ > 5.0f)
            {
                status(app);
                fail("the model was not prepared");
                return;
            }
        }
        else
            stalled_ = 0;
    }
    else if (step.verb == "expect")
    {
        // What must be true now for the rest of the run to mean anything.
        const std::size_t space = step.argument.find(' ');
        const std::string what = step.argument.substr(0, space);
        const std::string value =
            space == std::string::npos ? std::string() : step.argument.substr(space + 1);
        const bool answered = !state.messages.empty() && !state.retry_available &&
                              std::strcmp(state.messages.back().role, "assistant") == 0;
        const std::string last = state.messages.empty() ? "" : state.messages.back().content;
        bool met = false;
        if (what == "model")
            met = state.selected_model >= 0 && state.ready &&
                  state.models[static_cast<std::size_t>(state.selected_model)].id.find(value) !=
                      std::string::npos;
        else if (what == "answer")
            met = answered && !last.empty();
        else if (what == "image")
            met = answered && !state.images.empty() && last.find(".tga") != std::string::npos;
        else if (what == "audio")
            met = answered && last.find(".wav") != std::string::npos;
        if (!met)
        {
            status(app);
            fail("not as expected");
            return;
        }
        report("expect %s: yes", step.argument.c_str());
        done = true;
    }
    else if (step.verb == "press")
    {
        bool known = false;
        for (const Key &key : kKeys)
        {
            if (step.argument != key.name)
                continue;
            input.pressed |= hui::action_bit(key.action);
            if (key.direction != hui::Direction::none)
                input.nav = key.direction;
            known = true;
        }
        if (!known)
        {
            fail("no such button");
            return;
        }
        done = true;
    }
    else if (step.verb == "type")
    {
        // One character a frame, as a keyboard would send them.
        if (!entered_)
        {
            typing_ = step.argument;
            entered_ = true;
        }
        if (!typing_.empty())
        {
            ui.type(typing_.front(), feedback);
            typing_.erase(typing_.begin());
        }
        done = typing_.empty();
    }
    else if (step.verb == "backspace")
    {
        if (!entered_)
        {
            typing_.assign(static_cast<std::size_t>(std::max(step.number, 1.0f)), '\b');
            entered_ = true;
        }
        ui.backspace(feedback);
        typing_.pop_back();
        done = typing_.empty();
    }
    else if (step.verb == "submit")
    {
        ui.submit(feedback);
        done = true;
    }
    else if (step.verb == "scroll")
    {
        ui.scroll(step.number);
        done = true;
    }
    else if (step.verb == "shot")
    {
        capture_ = output_ + "/" + step.argument + ".bmp";
        done = true;
    }
    else if (step.verb == "status")
    {
        status(app);
        done = true;
    }
    else if (step.verb == "quit")
    {
        if (!entered_)
        {
            entered_ = true;
            status(app);
            // The app's storage is only readable from a PC while it runs: say that
            // the report is complete, then stay up long enough for it to be copied.
            report("%s  closing in %.0f s", failed_ ? "RESULT: FAILED" : "RESULT: COMPLETED",
                   static_cast<double>(step.number));
        }
        if (step_clock_ >= step.number && !app.busy())
        {
            quit_ = true;
            return;
        }
    }
    else
    {
        fail("no such step");
        return;
    }
    if (done)
    {
        if (step.verb == "press" || step.verb == "type" || step.verb == "submit" ||
            step.verb == "backspace")
            report("%s %s", step.verb.c_str(), step.argument.c_str());
        ++at_;
        entered_ = false;
        step_clock_ = 0;
    }
}
} // namespace prospero
