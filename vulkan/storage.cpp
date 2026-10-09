// ProsperoAI - Filesystem access and where the app keeps its files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef PROSPERO_UI_VULKAN
#include "storage.hpp"

#include "elevation/elevation.hpp"
#include "model_paths.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" int sceKernelDebugOutText(int, const char *);

namespace prospero
{
namespace
{
constexpr char kSandboxApp[] = "/app0";
constexpr char kSandboxDownload[] = "/download0";
constexpr char kSandboxData[] = "/download0/ProsperoAI";
constexpr char kSandboxSettings[] = "/download0/prosperoai.cfg";
constexpr char kData[] = "/data/prosperoai";
// Where earlier versions kept models: the helper-mounted folder of 01.001.000, and
// the app folder itself before that.
constexpr char kSharedModels[] = "/data/homebrew/prosperoai/models";

// Made on first use: a file may be opened before main(), when a global would not be
// constructed yet.
Storage &record()
{
    static Storage storage;
    return storage;
}
#define settled record()
bool real_paths; // the sandbox root is gone: sandbox names are turned into real ones

void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
void say(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    sceKernelDebugOutText(0, line);
}

bool is_file(const std::string &path)
{
    struct stat info
    {
    };
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool is_directory(const std::string &path)
{
    struct stat info
    {
    };
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string read_text(const std::string &path, std::size_t limit)
{
    std::string text;
    if (std::FILE *file = std::fopen(path.c_str(), "rb"))
    {
        text.resize(limit);
        text.resize(std::fread(text.data(), 1, limit, file));
        std::fclose(file);
    }
    return text;
}

// The string value of `key` in a flat JSON text, or empty.
std::string json_string(const std::string &text, const char *key)
{
    const std::string quoted = std::string("\"") + key + "\"";
    std::size_t at = text.find(quoted);
    if (at == std::string::npos)
        return {};
    at = text.find(':', at + quoted.size());
    const std::size_t open = at == std::string::npos ? at : text.find('"', at);
    const std::size_t close = open == std::string::npos ? open : text.find('"', open + 1);
    return close == std::string::npos ? std::string() : text.substr(open + 1, close - open - 1);
}

// The app's folder once the sandbox root is gone: /app0 while it is still there, else
// the console's mount of the running app (whatever it was mounted from), else the
// usual install folder.
std::string find_app_dir(const std::string &title)
{
    const std::string candidates[] = {kSandboxApp, "/system_ex/app/" + title,
                                      "/data/homebrew/" + title,
                                      "/mnt/sandbox/" + title + "_000/app0"};
    for (const std::string &candidate : candidates)
        if (is_file(candidate + "/eboot.bin"))
            return candidate;
    return kSandboxApp;
}

std::vector<std::string> names_in(const std::string &folder)
{
    std::vector<std::string> names;
    if (DIR *directory = opendir(folder.c_str()))
    {
        while (const dirent *entry = readdir(directory))
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0)
                names.emplace_back(entry->d_name);
        closedir(directory);
    }
    return names;
}

bool ends_with(const std::string &text, const char *end)
{
    const std::size_t length = std::strlen(end);
    return text.size() >= length && text.compare(text.size() - length, length, end) == 0;
}

// Models an earlier version installed are moved, not copied: they are gigabytes, and
// both places are on the same drive. What cannot be moved stays where it is.
unsigned adopt_models(const std::string &from)
{
    unsigned moved = 0;
    for (const std::string &name : names_in(from))
    {
        // Not a model: the old helper's marker, a download in progress, the folder's note.
        if (name[0] == '.' || name == "README.txt" || ends_with(name, ".part") ||
            ends_with(name, ".download"))
            continue;
        const std::string target = settled.model_root + "/" + name;
        struct stat present
        {
        };
        if (::lstat(target.c_str(), &present) == 0)
            continue;
        if (std::rename((from + "/" + name).c_str(), target.c_str()) == 0)
            ++moved;
        else
            say("[prosperoai] storage: %s/%s stays where it is (errno %d)\n", from.c_str(),
                name.c_str(), errno);
    }
    return moved;
}

bool copy_file(const std::string &from, const std::string &to)
{
    std::FILE *source = std::fopen(from.c_str(), "rb");
    if (!source)
        return false;
    std::FILE *target = std::fopen(to.c_str(), "wb");
    bool ok = target != nullptr;
    static char block[64 * 1024];
    while (ok)
    {
        const std::size_t read = std::fread(block, 1, sizeof(block), source);
        if (read == 0)
            break;
        ok = std::fwrite(block, 1, read, target) == read;
    }
    ok = ok && std::ferror(source) == 0;
    std::fclose(source);
    if (target && std::fclose(target) != 0)
        ok = false;
    if (!ok)
        std::remove(to.c_str());
    return ok;
}

void copy_tree(const std::string &from, const std::string &to, int depth)
{
    ::mkdir(to.c_str(), 0777);
    for (const std::string &name : names_in(from))
    {
        const std::string source = from + "/" + name;
        const std::string target = to + "/" + name;
        if (is_directory(source))
        {
            if (depth < 4)
                copy_tree(source, target, depth + 1);
        }
        else if (!is_file(target))
            copy_file(source, target);
    }
}

// Once: the settings and conversations the app kept in its sandbox are copied to the
// data folder (the originals stay, for a console where access is refused next time).
void adopt_sandbox_data()
{
    const std::string marker = settled.data_root + "/.sandbox-data-copied";
    if (is_file(marker))
        return;
    const std::string sandbox = "/mnt/sandbox/" + settled.title_id + "_000" + kSandboxDownload;
    const std::string settings = settled.data_root + "/prosperoai.cfg";
    if (is_file(sandbox + "/prosperoai.cfg") && !is_file(settings))
        copy_file(sandbox + "/prosperoai.cfg", settings);
    if (is_directory(sandbox + "/ProsperoAI/sessions"))
        copy_tree(sandbox + "/ProsperoAI/sessions", settled.data_root + "/sessions", 0);
    if (std::FILE *file = std::fopen(marker.c_str(), "wb"))
        std::fclose(file);
}

bool starts_with(const char *path, const char *prefix, const char **rest)
{
    const std::size_t length = std::strlen(prefix);
    if (std::strncmp(path, prefix, length) != 0 || (path[length] != '/' && path[length] != '\0'))
        return false;
    *rest = path + length;
    return true;
}
} // namespace

const Storage &prepare_storage()
{
    // The packaged param.json names the title; read it while /app0 is certain.
    const std::string param = read_text(std::string(kSandboxApp) + "/sce_sys/param.json", 4096);
    settled.title_id = json_string(param, "titleId");
    settled.version = json_string(param, "contentVersion");
    settled.access = static_cast<int>(elevation::request(elevation::Capability::filesystem));
    settled.route = elevation::path();
    // Elevation leaves the effective group apart from the real one; match them, as
    // files made from here on should belong to one group.
    if (settled.granted() && getegid() != getgid())
        (void)setegid(getgid());

    settled.app_dir = kSandboxApp;
    settled.data_root = kSandboxData;
    if (settled.granted() && !settled.title_id.empty())
    {
        settled.app_dir = find_app_dir(settled.title_id);
        // The folder is made here so that a console where it cannot be falls back to
        // the app's own storage instead of losing every conversation.
        ::mkdir(kData, 0777);
        if (is_directory(kData))
        {
            ::chmod(kData, 0777);
            settled.data_root = kData;
        }
        else
            settled.data_root =
                "/mnt/sandbox/" + settled.title_id + "_000" + std::string(kSandboxData);
        real_paths = true;
    }
    else
        ::mkdir(kSandboxData, 0777);
    settled.model_root = settled.data_root + "/models";
    for (const char *folder : {"/models", "/sessions", "/logs", "/work"})
        ::mkdir((settled.data_root + folder).c_str(), 0777);
    model_root_slot() = settled.model_root.c_str();

    if (settled.granted() && settled.data_root == kData)
    {
        settled.moved_models = adopt_models(kSharedModels);
        if (!settled.title_id.empty())
            settled.moved_models += adopt_models("/data/homebrew/" + settled.title_id + "/models");
        adopt_sandbox_data();
    }
    say("[prosperoai] storage: access=%d route=%s step=\"%s\" code=%d app=%s data=%s models=%s "
        "moved=%u\n",
        settled.access, settled.route, elevation::step(), elevation::step_code(),
        settled.app_dir.c_str(), settled.data_root.c_str(), settled.model_root.c_str(),
        settled.moved_models);
    return settled;
}

const Storage &storage()
{
    return settled;
}

std::string real_path(const char *path)
{
    if (!path)
        return {};
    const char *rest = nullptr;
    // The prebuilt sound runtime names its model's files under /app0/models, where
    // models were kept when it was built.
    if (starts_with(path, "/app0/models", &rest))
        return settled.model_root + rest;
    if (!real_paths)
        return path;
    if (starts_with(path, kSandboxApp, &rest))
        return settled.app_dir + rest;
    if (starts_with(path, kSandboxData, &rest))
        return settled.data_root + rest;
    if (std::strcmp(path, kSandboxSettings) == 0)
        return settled.data_root + "/prosperoai.cfg";
    // Whatever else was written to the sandbox's root: the runtimes' scratch files.
    if (starts_with(path, kSandboxDownload, &rest))
        return settled.data_root + "/work" + rest;
    return path;
}
} // namespace prospero
#endif
