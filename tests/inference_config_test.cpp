// SPDX-License-Identifier: GPL-3.0-or-later
#include "inference_config.hpp"
#include "reasoning_prompt.hpp"
#include <cassert>
int main(int argc, char **argv)
{
    assert(argc == 2);
    const std::string path = std::string(argv[1]) + "/test.txt";
    assert(prospero::read_inference_setting(argv[1], "absent.txt", 256, 32, 2048) == 256);
    const auto check = [&](const std::string &text, unsigned expected)
    {
        FILE *file = std::fopen(path.c_str(), "wb");
        assert(file);
        assert(std::fwrite(text.data(), 1, text.size(), file) == text.size());
        std::fclose(file);
        assert(prospero::read_inference_setting(argv[1], "test.txt", 256, 32, 2048) == expected);
    };
    check("512\n", 512);
    check(" \t2048\r\n", 2048);
    check("32", 32);
    for (const auto &text :
         {"", "31", "2049", "-1", "+512", "1 024", "512x", "99999999999999999999"})
        check(text, 256);
    check(std::string(64, '1'), 256);
    check(std::string("512\0oops", 8), 256);
    FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fputs("0", file);
    std::fclose(file);
    assert(prospero::read_inference_setting(argv[1], "test.txt", 1, 0, 2) == 0);
    const std::string assistant = "<|im_start|>assistant\n";
    const char *qwen_template = "enable_thinking <think>";
    std::string prompt = assistant;
    prospero::reasoning_prompt(prompt, qwen_template, false, true);
    assert(prompt == assistant + "<think>\n");
    prospero::reasoning_prompt(prompt, qwen_template, false, true);
    assert(prompt == assistant + "<think>\n");
    prompt = assistant;
    prospero::reasoning_prompt(prompt, qwen_template, false, false);
    assert(prompt == assistant + "<think>\n\n</think>\n\n");
    prompt = assistant;
    prospero::reasoning_prompt(prompt, "ordinary chatml", false, true);
    assert(prompt == assistant);
    prospero::reasoning_prompt(prompt, "ordinary chatml", false, false);
    assert(prompt == assistant);
    prospero::reasoning_prompt(prompt, nullptr, true, true);
    assert(prompt == assistant);
    prospero::reasoning_prompt(prompt, nullptr, true, false);
    assert(prompt == assistant + "<think>\n</think>\n\n");
}
