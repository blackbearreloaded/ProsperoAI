// ProsperoAI - A scripted, self-ending run for tests on a console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "native_ui.hpp"
#include <string>
#include <vector>

namespace prospero
{
// A console cannot be driven or closed from a PC without killing the app, so a
// test run is written down instead: the install folder's dev/request.txt (which
// a PC can write over FTP) lists steps, the app plays them as if a controller
// and a keyboard were used, writes what it saw to its own storage, waits for
// the PC to collect it, and closes itself the normal way. Without that file
// nothing here does anything. See docs/NATIVE_UI.md for the steps.
class DevScript
{
  public:
    // Reads the request. False when there is none, when it is not a script, or
    // when its token was already played by an earlier launch (remembered in
    // `output`, where the report and the pictures go).
    bool load(const std::string &request, const std::string &output);
    bool active() const
    {
        return loaded_ && !quit_;
    }
    // One frame, before the interface's own update: `seconds` is the real time
    // the last frame took. Adds the input the current step asks for.
    void update(float seconds, App &app, NativeUI &ui, hui::InputFrame &input,
                hui::ui::Feedback &feedback);
    // The picture the frame being drawn should be saved as (a path), or empty.
    const std::string &capture() const
    {
        return capture_;
    }
    void capture_done(bool saved);
    // False while a test has switched drawing off ("set draw off"): the interface
    // still runs, and nothing is drawn or shown.
    bool draw() const
    {
        return draw_;
    }
    // The run is over: leave the main loop and close as the app always does.
    bool quit() const
    {
        return quit_;
    }

  private:
    struct Step
    {
        std::string verb, argument;
        float number = 0;
    };
    void report(const char *format, ...);
    void status(App &app);
    void fail(const char *why);

    std::string output_, capture_, typing_;
    std::vector<Step> steps_;
    std::size_t at_ = 0;
    bool loaded_ = false, quit_ = false, failed_ = false, entered_ = false, draw_ = true;
    float clock_ = 0, step_clock_ = 0, limit_ = 300, stalled_ = 0;
    // Frame times since the last status line.
    unsigned frames_ = 0, slow_ = 0, unmet_ = 0;
    float frame_sum_ = 0, frame_worst_ = 0;
};
} // namespace prospero
