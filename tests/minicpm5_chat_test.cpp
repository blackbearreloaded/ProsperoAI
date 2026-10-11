// SPDX-License-Identifier: GPL-3.0-or-later
#include "minicpm5_chat.hpp"
#include <cassert>
#include <iostream>
#include <vector>
int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "chat") == 0)
    {
        assert(argc % 2 == 0);
        std::vector<gpt_runtime_message_t> messages;
        for (int i = 2; i < argc; i += 2)
            messages.push_back({argv[i], argv[i + 1]});
        std::string output;
        if (!prospero::minicpm5_chat(messages.data(), messages.size(), output))
            return 2;
        std::cout << output;
        return 0;
    }
    return 2;
}
