// ProsperoAI local session persistence.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session_store.hpp"

#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

extern "C"
{
    int sceKernelOpen(const char *, int, int);
    int sceKernelClose(int);
    int sceKernelGetdents(int, char *, int);
    int sceKernelMkdir(const char *, int);
    int sceKernelRmdir(const char *);
    int sceKernelUnlink(const char *);
}

namespace prospero_session
{
namespace
{

#if defined(PROSPERO_SESSION_TEST)
constexpr const char *kBase = "/tmp/prosperoai-session-test";
constexpr const char *kRoot = "/tmp/prosperoai-session-test/sessions";
#else
constexpr const char *kBase = "/download0/ProsperoAI";
constexpr const char *kRoot = "/download0/ProsperoAI/sessions";
#endif
alignas(16) char directory_entries[0x10000];

std::uint64_t now_microseconds()
{
    timeval now{};
    return gettimeofday(&now, nullptr) == 0 ? static_cast<std::uint64_t>(now.tv_sec) * 1000000ULL +
                                                  static_cast<std::uint64_t>(now.tv_usec)
                                            : 0;
}

void current_time(char *output, std::size_t capacity)
{
    timeval now{};
    struct timezone zone
    {
    };
    if (gettimeofday(&now, &zone) != 0)
    {
        std::snprintf(output, capacity, "--:--:--");
        return;
    }
    long long seconds =
        static_cast<long long>(now.tv_sec) - static_cast<long long>(zone.tz_minuteswest) * 60LL;
    seconds %= 24LL * 60LL * 60LL;
    if (seconds < 0)
        seconds += 24LL * 60LL * 60LL;
    std::snprintf(output, capacity, "%02lld:%02lld:%02lld", seconds / 3600LL,
                  (seconds / 60LL) % 60LL, seconds % 60LL);
}

void make_path(char *output, std::size_t capacity, const char *id, const char *leaf = nullptr)
{
    if (leaf)
        std::snprintf(output, capacity, "%s/%s/%s", kRoot, id, leaf);
    else
        std::snprintf(output, capacity, "%s/%s", kRoot, id);
}

void ensure_roots()
{
    sceKernelMkdir(kBase, 0777);
    sceKernelMkdir(kRoot, 0777);
}

bool read_file(const char *path, char *output, std::size_t capacity)
{
    if (!output || capacity < 2)
        return false;
    std::FILE *file = std::fopen(path, "rb");
    if (!file)
        return false;
    const std::size_t bytes = std::fread(output, 1, capacity - 1, file);
    output[bytes] = '\0';
    std::fclose(file);
    return true;
}

bool json_string(const char *json, const char *key, char *output, std::size_t capacity)
{
    char quoted[48];
    std::snprintf(quoted, sizeof(quoted), "\n  \"%s\":", key);
    const char *at = std::strstr(json, quoted);
    if (!at)
        return false;
    at += std::strlen(quoted);
    do
        ++at;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n');
    if (*at++ != '"')
        return false;
    std::size_t length = 0;
    while (*at && *at != '"' && length + 1 < capacity)
    {
        char value = *at++;
        if (value == '\\')
        {
            value = *at++;
            if (value == 'n')
                value = ' ';
            else if (value != '"' && value != '\\' && value != '/')
                return false;
        }
        if (static_cast<unsigned char>(value) < 32)
            return false;
        output[length++] = value;
    }
    if (*at != '"')
        return false;
    output[length] = '\0';
    return true;
}

bool json_u64(const char *json, const char *key, std::uint64_t *value)
{
    char quoted[48];
    std::snprintf(quoted, sizeof(quoted), "\n  \"%s\":", key);
    const char *at = std::strstr(json, quoted);
    if (!at)
        return false;
    at += std::strlen(quoted);
    do
        ++at;
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n');
    if (*at < '0' || *at > '9')
        return false;
    std::uint64_t parsed = 0;
    while (*at >= '0' && *at <= '9')
    {
        parsed = parsed * 10 + static_cast<unsigned>(*at - '0');
        ++at;
    }
    *value = parsed;
    return true;
}

void write_json_string(std::FILE *file, const char *value)
{
    std::fputc('"', file);
    for (const unsigned char *at = reinterpret_cast<const unsigned char *>(value ? value : ""); *at;
         ++at)
    {
        if (*at == '"' || *at == '\\')
            std::fputc('\\', file);
        if (*at >= 32)
            std::fputc(*at, file);
        else if (*at == '\n')
            std::fputs("\\n", file);
    }
    std::fputc('"', file);
}

bool read_metadata(const char *id, Record *record)
{
    if (!valid_id(id) || !record)
        return false;
    char path[256];
    char json[1024];
    make_path(path, sizeof(path), id, "session.json");
    if (!read_file(path, json, sizeof(json)))
        return false;
    Record parsed{};
    std::uint64_t schema = 0;
    std::uint64_t message_count = 0;
    std::uint64_t context_start = 0;
    if (!json_u64(json, "schema", &schema) || schema != 1 ||
        !json_string(json, "id", parsed.id, sizeof(parsed.id)) || std::strcmp(parsed.id, id) != 0 ||
        !json_string(json, "title", parsed.title, sizeof(parsed.title)) ||
        !json_string(json, "model_id", parsed.model_id, sizeof(parsed.model_id)) ||
        !json_string(json, "model_name", parsed.model_name, sizeof(parsed.model_name)) ||
        !json_string(json, "purpose", parsed.purpose, sizeof(parsed.purpose)) ||
        !json_string(json, "updated_time", parsed.updated_time, sizeof(parsed.updated_time)) ||
        !json_u64(json, "created_us", &parsed.created_us) ||
        !json_u64(json, "updated_us", &parsed.updated_us) ||
        !json_u64(json, "message_count", &message_count) ||
        !json_u64(json, "context_start", &context_start) || message_count > MessageCapacity ||
        context_start > message_count)
        return false;
    parsed.message_count = static_cast<unsigned>(message_count);
    parsed.context_start = static_cast<unsigned>(context_start);
    *record = parsed;
    return true;
}

bool write_message(const char *id, unsigned index, const Message &message)
{
    char leaf[40];
    char path[256];
    char temporary[272];
    std::snprintf(leaf, sizeof(leaf), "message-%03u.txt", index);
    make_path(path, sizeof(path), id, leaf);
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    std::FILE *file = std::fopen(temporary, "wb");
    if (!file)
        return false;
    const std::size_t content_length = std::strlen(message.content);
    bool written = std::fprintf(file, "%s\n%s\n", message.role, message.timestamp) > 0 &&
                   std::fwrite(message.content, 1, content_length, file) == content_length;
    written = std::fclose(file) == 0 && written;
    if (!written)
    {
        std::remove(temporary);
        return false;
    }
    if (std::rename(temporary, path) != 0)
    {
        std::remove(temporary);
        return false;
    }
    return true;
}

bool read_message(const char *id, unsigned index, Message *message)
{
    char leaf[40];
    char path[256];
    std::snprintf(leaf, sizeof(leaf), "message-%03u.txt", index);
    make_path(path, sizeof(path), id, leaf);
    std::FILE *file = std::fopen(path, "rb");
    if (!file)
        return false;
    Message parsed{};
    char role[32]{};
    char timestamp[32]{};
    if (!std::fgets(role, sizeof(role), file) || !std::fgets(timestamp, sizeof(timestamp), file))
    {
        std::fclose(file);
        return false;
    }
    role[std::strcspn(role, "\r\n")] = '\0';
    timestamp[std::strcspn(timestamp, "\r\n")] = '\0';
    if ((std::strcmp(role, "user") != 0 && std::strcmp(role, "assistant") != 0) ||
        std::strlen(timestamp) != 8)
    {
        std::fclose(file);
        return false;
    }
    std::memcpy(parsed.role, role, std::strlen(role) + 1);
    std::memcpy(parsed.timestamp, timestamp, 9);
    const std::size_t bytes = std::fread(parsed.content, 1, sizeof(parsed.content) - 1, file);
    parsed.content[bytes] = '\0';
    std::fclose(file);
    *message = parsed;
    return true;
}

bool remove_tree(const char *path, unsigned depth)
{
    if (!path || depth > 3)
        return false;
    const int directory = sceKernelOpen(path, 0, 0);
    if (directory >= 0)
    {
        char entries[4096];
        bool ok = true;
        for (;;)
        {
            const int bytes = sceKernelGetdents(directory, entries, sizeof(entries));
            if (bytes <= 0)
            {
                if (bytes < 0)
                    ok = false;
                break;
            }
            int offset = 0;
            while (offset + static_cast<int>(offsetof(dirent, d_name)) + 1 <= bytes)
            {
                const auto *entry = reinterpret_cast<const dirent *>(entries + offset);
                if (entry->d_reclen <= offsetof(dirent, d_name) || offset + entry->d_reclen > bytes)
                {
                    ok = false;
                    break;
                }
                if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
                {
                    char child[320];
                    std::snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
                    if (sceKernelUnlink(child) != 0 && !remove_tree(child, depth + 1))
                    {
                        ok = false;
                        break;
                    }
                }
                offset += entry->d_reclen;
            }
            if (!ok)
                break;
        }
        sceKernelClose(directory);
        if (!ok)
            return false;
    }
    return sceKernelRmdir(path) == 0;
}

} // namespace

bool valid_id(const char *id)
{
    if (!id || !*id || std::strlen(id) >= sizeof(Record{}.id))
        return false;
    for (const char *at = id; *at; ++at)
        if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') ||
              (*at >= '0' && *at <= '9') || *at == '-'))
            return false;
    return true;
}

bool create(Record *record, const char *model_id, const char *model_name, const char *purpose)
{
    if (!record || !model_id || !model_name || !purpose)
        return false;
    ensure_roots();
    Record created{};
    created.created_us = created.updated_us = now_microseconds();
    std::snprintf(created.id, sizeof(created.id), "session-%llu",
                  static_cast<unsigned long long>(created.created_us));
    std::snprintf(created.title, sizeof(created.title), "New conversation");
    std::snprintf(created.model_id, sizeof(created.model_id), "%s", model_id);
    std::snprintf(created.model_name, sizeof(created.model_name), "%s", model_name);
    std::snprintf(created.purpose, sizeof(created.purpose), "%s", purpose);
    current_time(created.updated_time, sizeof(created.updated_time));
    char directory[256];
    char media[272];
    make_path(directory, sizeof(directory), created.id);
    make_path(media, sizeof(media), created.id, "media");
    if (sceKernelMkdir(directory, 0777) != 0 || sceKernelMkdir(media, 0777) != 0)
        return false;
    *record = created;
    return true;
}

bool save(Record *record, const Message *messages, unsigned message_count)
{
    if (!record || !valid_id(record->id) || (message_count && !messages) ||
        message_count > MessageCapacity)
        return false;
    record->message_count = message_count;
    if (record->context_start > message_count)
        record->context_start = message_count;
    record->updated_us = now_microseconds();
    current_time(record->updated_time, sizeof(record->updated_time));
    for (unsigned index = 0; index < message_count; ++index)
        if (!write_message(record->id, index, messages[index]))
            return false;

    char path[256];
    char temporary[272];
    make_path(path, sizeof(path), record->id, "session.json");
    std::snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    std::FILE *file = std::fopen(temporary, "wb");
    if (!file)
        return false;
    std::fputs("{\n  \"schema\": 1,\n  \"id\": ", file);
    write_json_string(file, record->id);
    std::fputs(",\n  \"title\": ", file);
    write_json_string(file, record->title);
    std::fputs(",\n  \"model_id\": ", file);
    write_json_string(file, record->model_id);
    std::fputs(",\n  \"model_name\": ", file);
    write_json_string(file, record->model_name);
    std::fputs(",\n  \"purpose\": ", file);
    write_json_string(file, record->purpose);
    std::fputs(",\n  \"updated_time\": ", file);
    write_json_string(file, record->updated_time);
    std::fprintf(file,
                 ",\n  \"created_us\": %llu,\n  \"updated_us\": %llu,\n"
                 "  \"message_count\": %u,\n  \"context_start\": %u\n}\n",
                 static_cast<unsigned long long>(record->created_us),
                 static_cast<unsigned long long>(record->updated_us), record->message_count,
                 record->context_start);
    if (std::fclose(file) != 0 || std::rename(temporary, path) != 0)
    {
        std::remove(temporary);
        return false;
    }
    return true;
}

bool load(const char *id, Record *record, Message *messages, unsigned message_capacity)
{
    Record loaded{};
    if (!read_metadata(id, &loaded) || loaded.message_count > message_capacity ||
        (loaded.message_count && !messages))
        return false;
    for (unsigned index = 0; index < loaded.message_count; ++index)
        if (!read_message(id, index, &messages[index]))
            return false;
    *record = loaded;
    return true;
}

unsigned scan(Record *records, unsigned capacity)
{
    if (!records || !capacity)
        return 0;
    ensure_roots();
    const int directory = sceKernelOpen(kRoot, 0, 0);
    if (directory < 0)
        return 0;
    unsigned count = 0;
    for (;;)
    {
        std::memset(directory_entries, 0, sizeof(directory_entries));
        const int bytes =
            sceKernelGetdents(directory, directory_entries, sizeof(directory_entries));
        if (bytes <= 0)
            break;
        int offset = 0;
        while (offset + static_cast<int>(offsetof(dirent, d_name)) + 1 <= bytes)
        {
            const auto *entry = reinterpret_cast<const dirent *>(directory_entries + offset);
            if (entry->d_reclen <= offsetof(dirent, d_name) || offset + entry->d_reclen > bytes)
                break;
            Record record{};
            if (read_metadata(entry->d_name, &record))
            {
                if (count < capacity)
                    records[count++] = record;
                else
                {
                    unsigned oldest = 0;
                    for (unsigned index = 1; index < count; ++index)
                        if (records[index].updated_us < records[oldest].updated_us)
                            oldest = index;
                    if (record.updated_us > records[oldest].updated_us)
                        records[oldest] = record;
                }
            }
            offset += entry->d_reclen;
        }
    }
    sceKernelClose(directory);
    for (unsigned i = 1; i < count; ++i)
        for (unsigned j = i; j > 0 && records[j - 1].updated_us < records[j].updated_us; --j)
        {
            const Record swap = records[j - 1];
            records[j - 1] = records[j];
            records[j] = swap;
        }
    return count;
}

bool erase(const char *id)
{
    if (!valid_id(id))
        return false;
    char path[256];
    make_path(path, sizeof(path), id);
    return remove_tree(path, 0);
}

bool archive_media(const char *id, unsigned message_index, const char *kind, const char *source,
                   char *output, std::size_t output_capacity)
{
    if (!valid_id(id) || message_index >= MessageCapacity || !kind ||
        (std::strcmp(kind, "image") != 0 && std::strcmp(kind, "audio") != 0) || !source ||
        !output || output_capacity < 2)
        return false;
    const char *extension = std::strrchr(source, '.');
    if (!extension || std::strlen(extension) > 8)
        return false;
    char leaf[64];
    std::snprintf(leaf, sizeof(leaf), "media/%s-%03u%s", kind, message_index, extension);
    make_path(output, output_capacity, id, leaf);
    std::FILE *input = std::fopen(source, "rb");
    std::FILE *destination = input ? std::fopen(output, "wb") : nullptr;
    if (!input || !destination)
    {
        if (input)
            std::fclose(input);
        if (destination)
            std::fclose(destination);
        return false;
    }
    char buffer[32768];
    bool ok = true;
    while (ok)
    {
        const std::size_t bytes = std::fread(buffer, 1, sizeof(buffer), input);
        if (bytes && std::fwrite(buffer, 1, bytes, destination) != bytes)
            ok = false;
        if (bytes < sizeof(buffer))
        {
            if (std::ferror(input))
                ok = false;
            break;
        }
    }
    ok = std::fclose(input) == 0 && std::fclose(destination) == 0 && ok;
    if (!ok)
        std::remove(output);
    return ok;
}

} // namespace prospero_session
