// SPDX-License-Identifier: GPL-3.0-or-later
#include "voice_recorder.hpp"
#include "runtime_voice_catalog.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>

namespace { std::atomic<int> mode{0}, closed{0}; }
extern "C" {
int sceUserServiceGetInitialUser(int *user) { *user=123; return 0; }
int sceAudioInInit() { return 0; }
int sceAudioInOpen(int user,unsigned type,unsigned index,unsigned frames,unsigned rate,unsigned format) {
    assert(user==123 && type==1 && index==0 && frames==256 && rate==16000 && format==0);
    return mode==2 ? -1 : 7;
}
int sceAudioInClose(int handle) { assert(handle==7);closed++;return 0; }
int sceAudioInInput(int handle,void *buffer) {
    assert(handle==7);std::this_thread::sleep_for(std::chrono::microseconds(500));
    if(mode==3)return -1;
    auto *pcm=static_cast<std::int16_t *>(buffer);
    for(int i=0;i<256;i++)pcm[i]=mode==1 ? 0 : (i%2 ? -1200 : 1200);
    return 256;
}
}
void wait(prospero::VoiceRecorder &recorder) {
    for(int i=0;i<5000 && recorder.state()==prospero::VoiceRecorder::State::Recording;i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(recorder.state()!=prospero::VoiceRecorder::State::Recording);
}
int main() {
    assert(!prospero_voice::chatterbox_voice_deletable({"cemo","Cemo","English","demo_1_man.wav"}));
    assert(!prospero_voice::chatterbox_voice_deletable({"demo_1_man","Alias","Added voice","demo_1_man.wav"}));
    assert(!prospero_voice::chatterbox_voice_deletable({"escape","Escape","Added voice","../escape.wav"}));
    assert(prospero_voice::chatterbox_voice_deletable({"custom","My voice","Uploaded WAV","custom.wav"}));
    assert(prospero_voice::chatterbox_voice_deletable({"sample","Sample","Added voice","sample.wav"}));
    const auto selected=prospero_voice::runtime_voice(true,0);
    (void)prospero_voice::runtime_voice(true,1);
    assert(selected && selected->id=="cemo");
    using State=prospero::VoiceRecorder::State;
    prospero::VoiceRecorder recorder;
    assert(recorder.start());assert(!recorder.start());wait(recorder);
    assert(recorder.state()==State::Ready && recorder.seconds()==5 && closed==1);
    recorder.discard();assert(recorder.state()==State::Idle && recorder.seconds()==0);
    mode=1;assert(recorder.start());wait(recorder);assert(recorder.state()==State::Failed);assert(recorder.error().find("No voice")!=std::string::npos);
    mode=2;assert(recorder.start());wait(recorder);assert(recorder.state()==State::Failed && closed==2);
    mode=3;assert(recorder.start());wait(recorder);assert(recorder.state()==State::Failed && closed==3);
    mode=0;assert(recorder.start());recorder.stop();wait(recorder);assert(recorder.state()==State::Failed);
    assert(recorder.start());while(recorder.seconds()<1.2f)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    recorder.stop();wait(recorder);assert(recorder.state()==State::Ready && recorder.seconds()<5);
    recorder.discard();assert(closed==5);
}
