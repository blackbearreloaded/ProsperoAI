// ProsperoAI - Playing a generated sound.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace hui::audio
{
class Mixer;
}

// Generated sounds are played through the interface's mixer, which holds the title's
// one main audio port. Called once the mixer exists, and with nullptr before it goes.
void prospero_media_attach(hui::audio::Mixer *mixer);

extern "C"
{
    // 0 when the sound started. The file is read and converted before this returns.
    int ps5_media_play_wav(const char *path);
    int ps5_media_is_playing();
    void ps5_media_stop();
    void ps5_media_shutdown();
}
