// ProsperoAI - Where the prebuilt sound runtime finds its model.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// vendor/lib/libstable-audio.a was built when models lived in the app folder and
// names every one of its files under /app0/models. They are kept in the shared model
// folder now, so the library opened nothing and gave up at once.
// tools/prepare-hybrid-media.py sends the library's three ways of opening a file here,
// where such a path is turned into the one the file has today.
#ifdef PROSPERO_HYBRID_MEDIA
#include "model_paths.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>

namespace
{
constexpr char kBuiltWith[] = "/app0/models/";

// False when `path` is not one the library was built with; else `moved` is where it is now.
bool relocate(const char *path, std::string &moved)
{
    if (!path || std::strncmp(path, kBuiltWith, sizeof(kBuiltWith) - 1) != 0)
        return false;
    moved = std::string(prospero::kModelRoot) + "/" + (path + sizeof(kBuiltWith) - 1);
    return true;
}

// std::filebuf::open(const char *, std::ios_base::openmode) of the C++ runtime.
extern "C" void *
filebuf_open(void *buffer, const char *path,
             unsigned mode) __asm__("_ZNSt3__113basic_filebufIcNS_11char_traitsIcEEE4openEPKcj");
} // namespace

extern "C" std::FILE *prospero_path_fopen(const char *path, const char *mode)
{
    std::string moved;
    return std::fopen(relocate(path, moved) ? moved.c_str() : path, mode);
}

extern "C" int prospero_path_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    std::string moved;
    return open(relocate(path, moved) ? moved.c_str() : path, flags, mode);
}

extern "C" void *prospero_path_filebuf_open(void *buffer, const char *path, unsigned mode)
{
    std::string moved;
    return filebuf_open(buffer, relocate(path, moved) ? moved.c_str() : path, mode);
}
#endif
