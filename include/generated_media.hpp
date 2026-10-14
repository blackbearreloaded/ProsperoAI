// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Which files the app will play back. Generated speech lives in work/voice, music and sound
// effects in work/audio, and a conversation's own copies under sessions/. A path that comes
// from a message is not trusted: only these folders, no traversal, and only .wav.
#include <string>

namespace prospero_media {

inline bool playable_audio_path(const std::string &path)
{
    const bool generated = path.rfind("/download0/ProsperoAI/work/voice/", 0) == 0 ||
                           path.rfind("/download0/ProsperoAI/work/audio/", 0) == 0 ||
                           path.rfind("/download0/ProsperoAI/sessions/", 0) == 0;
    if (!generated || path.find("..") != std::string::npos || path.size() < 5 ||
        path.compare(path.size() - 4, 4, ".wav") != 0)
        return false;
    const auto slash = path.find_last_of('/');
    return slash != std::string::npos && path.size() - slash > 5; // a name before ".wav"
}

} // namespace prospero_media
