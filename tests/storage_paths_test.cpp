// SPDX-License-Identifier: GPL-3.0-or-later
#define PROSPERO_UI_VULKAN
#include "../vulkan/storage.cpp"
#include <cassert>
#include <filesystem>
#include <fstream>

int main(int argc, char **argv)
{
    assert(argc == 2);
    auto &state = prospero::record();
    state.data_root = "/download0/ProsperoAI";
    state.model_root = state.data_root + "/models";
    prospero::real_paths = false;
    assert(prospero::real_path("/download0/prosperoai-audio.wav") ==
           state.data_root + "/work/tmp/prosperoai-audio.wav");
    assert(prospero::real_path("/download0/ProsperoAI/work/voice/result.wav") ==
           "/download0/ProsperoAI/work/voice/result.wav");
    assert(prospero::real_path("/download0/prosperoai.cfg") == "/download0/prosperoai.cfg");
    prospero::real_paths = true;
    state.data_root = "/data/prosperoai";
    state.app_dir = "/data/homebrew/test-folder";
    state.model_root = state.data_root + "/models";
    assert(prospero::real_path("/download0/ProsperoAI/work/voice/result.wav") ==
           state.data_root + "/work/voice/result.wav");
    assert(prospero::real_path("/download0/ProsperoAI/work/audio/result.wav") ==
           state.data_root + "/work/audio/result.wav");
    assert(prospero::real_path("/download0/ProsperoAI/work/image/result.png") ==
           state.data_root + "/work/image/result.png");
    assert(prospero::real_path("/download0/prosperoai-audio.wav") ==
           state.data_root + "/work/tmp/prosperoai-audio.wav");
    assert(prospero::real_path("/download0/ProsperoAI/sessions/old/media/a.wav") ==
           state.data_root + "/sessions/old/media/a.wav");
    assert(prospero::real_path("/app0/models/model.gguf") == state.model_root + "/model.gguf");
    assert(prospero::real_path("/download00/untouched") == "/download00/untouched");
    state.data_root = "/mnt/sandbox/test-slot_000/download0/ProsperoAI";
    assert(prospero::real_path("/download0/ProsperoAI/work/image/result.tga") ==
           state.data_root + "/work/image/result.tga");
    // Data adoption must keep existing results and report failed copies for a retry.
    const std::string root = argv[1], source = root + "/source", target = root + "/target";
    std::filesystem::create_directories(source + "/session-id");
    std::filesystem::create_directories(target + "/session-id");
    {
        std::ofstream file(source + "/session-id/result.txt");
        file << "new";
    }
    {
        std::ofstream file(target + "/session-id/result.txt");
        file << "existing";
    }
    {
        std::ofstream file(source + "/session-id/result.txt.json");
        file << "metadata";
    }
    assert(prospero::copy_tree(source, target, 0));
    std::string bytes;
    {
        std::ifstream file(target + "/session-id/result.txt");
        file >> bytes;
    }
    assert(bytes == "existing");
    assert(std::filesystem::exists(target + "/session-id/result.txt.json"));
    {
        std::ofstream file(root + "/blocked");
        file << "file, not directory";
    }
    assert(!prospero::copy_tree(source, root + "/blocked", 0));
}
