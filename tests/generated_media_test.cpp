// SPDX-License-Identifier: GPL-3.0-or-later
// The app plays back only the audio it generated itself.
#include "generated_media.hpp"

#include <cassert>

int main()
{
    // Speech, music and a conversation's own copy are all playable.
    assert(prospero_media::playable_audio_path(
        "/download0/ProsperoAI/work/voice/chatterbox-speech-1.wav"));
    assert(
        prospero_media::playable_audio_path("/download0/ProsperoAI/work/audio/ace-step-1.5-1.wav"));
    assert(
        prospero_media::playable_audio_path("/download0/ProsperoAI/work/audio/stable-audio-1.wav"));
    assert(prospero_media::playable_audio_path(
        "/download0/ProsperoAI/sessions/chat-1/media/clip.wav"));

    // Another folder, a traversal, a different suffix or an empty path are not.
    assert(!prospero_media::playable_audio_path("/download0/ProsperoAI/work/image/picture.wav"));
    assert(!prospero_media::playable_audio_path("/data/prosperoai/work/audio/outside-sandbox.wav"));
    assert(!prospero_media::playable_audio_path(
        "/download0/ProsperoAI/work/audio/../../../etc/passwd.wav"));
    assert(!prospero_media::playable_audio_path("/download0/ProsperoAI/work/audio/track.tga"));
    assert(!prospero_media::playable_audio_path("/download0/ProsperoAI/work/audio/.wav"));
    assert(!prospero_media::playable_audio_path(""));
    return 0;
}
