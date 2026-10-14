// ProsperoAI - Saved conversations are read back as they were written.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_store.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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
    int sceKernelGetdents(int fd, char *buffer, int size)
    {
        return static_cast<int>(syscall(SYS_getdents64, fd, buffer, size));
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
    written[0].content = "Reverse a linked list";
    std::snprintf(written[1].role, sizeof(written[1].role), "assistant");
    std::snprintf(written[1].timestamp, sizeof(written[1].timestamp), "02:09");
    written[1].content.assign(400000, 'x');
    assert(save(&record, written, 2));

    Record loaded{};
    Message read[4]{};
    assert(load(record.id, &loaded, read, 4));
    assert(loaded.message_count == 2 && std::strcmp(loaded.model_id, model) == 0);
    assert(std::strcmp(read[0].role, "user") == 0 && std::strcmp(read[0].timestamp, "02:08") == 0);
    assert(std::strcmp(read[1].timestamp, "02:09") == 0);
    assert(read[1].content == written[1].content);

    assert(matches(loaded, "text", "LINKED") == false); // title is still default
    assert(matches(loaded, "text", "QWEN"));
    assert(!matches(loaded, "image", "Qwen"));
    std::snprintf(loaded.title, sizeof(loaded.title), "Linked list example");
    assert(matches(loaded, "all", "LIST"));
    for (const char *purpose : {"text-to-image", "text-to-audio", "text-to-speech"})
    {
        std::snprintf(loaded.purpose, sizeof(loaded.purpose), "%s", purpose);
        assert(!matches(loaded, "text", ""));
    }
    const char *source = "/tmp/prosperoai-session-test/source.wav";
    auto *file = std::fopen(source, "wb");
    assert(file);
    std::fputs("wave fixture", file);
    std::fclose(file);
    char media[320], second[320];
    OutputMetadata metadata;
    metadata.model_id = model;
    metadata.model_name = "Fixture";
    metadata.prompt = "First line\nSecond\tline \"quoted\"";
    metadata.output_limit = 192;
    metadata.requested_context = 32768;
    metadata.thinking = true;
    assert(archive_media(record.id, 1, "voice", source, media, sizeof(media), &metadata));
    const auto read_file = [](const std::string &path)
    {
        auto *input = std::fopen(path.c_str(), "rb");
        assert(input);
        std::string bytes;
        char buffer[1024];
        while (auto count = std::fread(buffer, 1, sizeof(buffer), input))
            bytes.append(buffer, count);
        std::fclose(input);
        return bytes;
    };
    assert(read_file(media) == "wave fixture");
    assert(!std::filesystem::exists(std::string(media) + ".json"));
    assert(read_file(std::string("/tmp/prosperoai-session-test/sessions/") + record.id +
                     "/output-001.txt") == std::string(media) + "\n");
    char text_output[320];
    assert(archive_text(record.id, 3, "Text result", metadata, text_output, sizeof(text_output)));
    assert(std::strstr(text_output, "/work/text/") && read_file(text_output) == "Text result");
    assert(!std::filesystem::exists(std::string(text_output) + ".json"));
    assert(read_file(std::string("/tmp/prosperoai-session-test/sessions/") + record.id +
                     "/output-003.txt") == std::string(text_output) + "\n");
    assert(std::strstr(media, "/work/voice/") != nullptr);
    usleep(1000);
    assert(archive_media(record.id, 1, "voice", source, second, sizeof(second)));
    assert(std::strcmp(media, second) != 0);
    assert(read_file(media) == "wave fixture" && read_file(second) == "wave fixture");
    assert(!std::filesystem::exists(std::string(second) + ".json"));
    assert(!archive_media(record.id, 1, "../audio", source, second, sizeof(second)));
    char too_short[4];
    assert(!archive_media(record.id, 1, "audio", source, too_short, sizeof(too_short)));

    // With seconds, as the first interface wrote them.
    std::snprintf(written[0].timestamp, sizeof(written[0].timestamp), "02:08:13");
    assert(save(&record, written, 2) && load(record.id, &loaded, read, 4));
    assert(std::strcmp(read[0].timestamp, "02:08:13") == 0);

    // Anything else is not a time, and the conversation is refused whole.
    std::snprintf(written[0].timestamp, sizeof(written[0].timestamp), "2:08");
    assert(save(&record, written, 2) && !load(record.id, &loaded, read, 4));
    assert(!load("session-missing", &loaded, read, 4));
    assert(erase(record.id));
    assert(access(media, F_OK) != 0);
    assert(access(text_output, F_OK) != 0);
    assert(!load(record.id, &loaded, read, 4));
}
