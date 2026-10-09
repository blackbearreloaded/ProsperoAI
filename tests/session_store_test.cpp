// ProsperoAI - Saved conversations are read back as they were written.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_store.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// The console's file calls, on the PC's.
extern "C"
{
    int sceKernelOpen(const char *path, int, int)
    {
        return open(path, O_RDONLY);
    }
    int sceKernelClose(int descriptor)
    {
        return close(descriptor);
    }
    int sceKernelGetdents(int, char *, int)
    {
        return 0;
    }
    int sceKernelMkdir(const char *path, int mode)
    {
        return mkdir(path, static_cast<mode_t>(mode));
    }
    int sceKernelRmdir(const char *path)
    {
        return rmdir(path);
    }
    int sceKernelUnlink(const char *path)
    {
        return unlink(path);
    }
}

int main()
{
    using namespace prospero_session;
    // A downloaded file's ID is longer than the 47 characters a record once had room for.
    const char *model = "Qwen-Qwen2.5-0.5B-Instruct-GGUF--qwen2.5-0.5b-instruct-q2_k.gguf";
    Record record{};
    assert(create(&record, model, model, "text-to-text"));
    assert(std::strcmp(record.model_id, model) == 0 && std::strcmp(record.model_name, model) == 0);

    // Hours and minutes, as the interface stamps a message.
    Message written[2]{};
    std::snprintf(written[0].role, sizeof(written[0].role), "user");
    std::snprintf(written[0].timestamp, sizeof(written[0].timestamp), "02:08");
    std::snprintf(written[0].content, sizeof(written[0].content), "Reverse a linked list");
    std::snprintf(written[1].role, sizeof(written[1].role), "assistant");
    std::snprintf(written[1].timestamp, sizeof(written[1].timestamp), "02:09");
    std::snprintf(written[1].content, sizeof(written[1].content),
                  "Walk it once.\nKeep three pointers.");
    assert(save(&record, written, 2));

    Record loaded{};
    Message read[4]{};
    assert(load(record.id, &loaded, read, 4));
    assert(loaded.message_count == 2 && std::strcmp(loaded.model_id, model) == 0);
    assert(std::strcmp(read[0].role, "user") == 0 && std::strcmp(read[0].timestamp, "02:08") == 0);
    assert(std::strcmp(read[1].timestamp, "02:09") == 0);
    assert(std::strcmp(read[1].content, written[1].content) == 0);

    // With seconds, as the first interface wrote them.
    std::snprintf(written[0].timestamp, sizeof(written[0].timestamp), "02:08:13");
    assert(save(&record, written, 2) && load(record.id, &loaded, read, 4));
    assert(std::strcmp(read[0].timestamp, "02:08:13") == 0);

    // Anything else is not a time, and the conversation is refused whole.
    std::snprintf(written[0].timestamp, sizeof(written[0].timestamp), "2:08");
    assert(save(&record, written, 2) && !load(record.id, &loaded, read, 4));
    assert(!load("session-missing", &loaded, read, 4));
}
