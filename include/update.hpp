// ProsperoAI - A newer version: asking the catalog, and installing it in place.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

// The mechanism of the other Prospero apps (third_party/update-check). Once per launch the
// app asks the homebrew.page catalog whether a newer ProsperoAI is listed; the kit verifies
// the catalog's signature before it believes the answer. When the user says so, the release
// ZIP is downloaded from GitHub and handed to the self-update helper (self-updater.elf in
// the app's folder, sent to the console's payload loader), which checks and unpacks it
// beside the app and replaces the app's files once the app has closed. Nothing is changed
// before the user confirms; a failure or a cancel leaves the app as it was.
namespace prospero::update
{
struct Offer
{
    bool installable = false; // the app can download and install it by itself
    char version[40] = {};    // the release's name, for display
    std::uint64_t size = 0;   // the ZIP's size in bytes; 0 when the catalog does not say
    // What the developer wrote on the release, as the catalog gives it: plain text, lines
    // split by '\n', list items starting "- ". Empty when there is none.
    std::string notes;
    bool notes_truncated = false; // the catalog cut them; the rest is on the app's page
};

enum class Phase : std::uint8_t
{
    idle,
    starting,    // starting the helper
    downloading, // done/total are bytes of the archive
    unpacking,   // done/total are bytes unpacked
    ready,       // staged and checked: apply or cancel
    applying,    // the helper has the go-ahead: the app closes
    cancelled,
    failed, // error says why; nothing was changed
};

struct Progress
{
    Phase phase = Phase::idle;
    std::uint64_t done = 0;
    std::uint64_t total = 0; // 0 while it is not known
    char time_left[32] = {}; // "about 20 s left"; empty until it can be said
    char error[160] = {};
};

// Asks the catalog on a thread of its own. Once per launch; later calls do nothing.
void check();
// The catalog's answer, handed over once: true when a newer release is listed.
bool take_offer(Offer *offer);
// Starts the download and the staging on a thread of its own.
bool begin();
// Where the update is; cheap, for every frame.
void poll(Progress *progress);
// Stops it; the app stays as it was.
void cancel();
// Once staged: gives the helper the go-ahead. True: close the app now.
bool apply();
// After a cancel or a failure, before beginning again.
void finish();
} // namespace prospero::update
