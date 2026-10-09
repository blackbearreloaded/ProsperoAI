// ProsperoAI - Sandbox file names, opened where the files really are.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// With filesystem access the app's sandbox root is gone: /app0 and /download0 name
// nothing any more. The sources could be taught the real folders, but the prebuilt
// model runtimes cannot: the sound and voice libraries carry their file names inside
// them. So the functions that take a path are wrapped at link time
// (tools/build-vulkan-native.sh), for every object of the program alike, and a
// sandbox name is turned into the real one here (prospero::real_path). Any other
// path, and every path while the app is still in its sandbox, passes through.
#ifdef PROSPERO_UI_VULKAN
#include "storage.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>

extern "C"
{
    std::FILE *__real_fopen(const char *, const char *);
    std::FILE *__real_freopen(const char *, const char *, std::FILE *);
    int __real_open(const char *, int, ...);
    int __real_stat(const char *, struct stat *);
    int __real_lstat(const char *, struct stat *);
    int __real_mkdir(const char *, mode_t);
    int __real_rmdir(const char *);
    int __real_unlink(const char *);
    int __real_remove(const char *);
    int __real_rename(const char *, const char *);
    int __real_access(const char *, int);
    DIR *__real_opendir(const char *);
    int __real_sceKernelOpen(const char *, int, int);
    int __real_sceKernelMkdir(const char *, int);
    int __real_sceKernelRmdir(const char *);
    int __real_sceKernelUnlink(const char *);
}

namespace
{
// A sandbox name starts with one of two words; everything else is left alone
// without a copy being made.
bool sandbox_name(const char *path)
{
    return path &&
           (std::strncmp(path, "/app0", 5) == 0 || std::strncmp(path, "/download0", 10) == 0);
}

// A file that must not be made or opened: see prospero::private_data_path.
bool refused(const char *path)
{
    if (!prospero::private_data_path(path))
        return false;
    errno = EACCES;
    return true;
}

// `path`, or its real name in `real` when it is a sandbox name.
const char *resolve(const char *path, std::string &real)
{
    if (!sandbox_name(path))
        return path;
    real = prospero::real_path(path);
    return real.c_str();
}
} // namespace

extern "C" std::FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (refused(path))
        return nullptr;
    std::string real;
    return __real_fopen(resolve(path, real), mode);
}

extern "C" std::FILE *__wrap_freopen(const char *path, const char *mode, std::FILE *stream)
{
    std::string real;
    return __real_freopen(resolve(path, real), mode, stream);
}

extern "C" int __wrap_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, int);
        va_end(arguments);
    }
    if (refused(path))
        return -1;
    std::string real;
    return __real_open(resolve(path, real), flags, mode);
}

extern "C" int __wrap_stat(const char *path, struct stat *info)
{
    std::string real;
    return __real_stat(resolve(path, real), info);
}

extern "C" int __wrap_lstat(const char *path, struct stat *info)
{
    std::string real;
    return __real_lstat(resolve(path, real), info);
}

extern "C" int __wrap_mkdir(const char *path, mode_t mode)
{
    if (refused(path))
        return -1;
    std::string real;
    return __real_mkdir(resolve(path, real), mode);
}

extern "C" int __wrap_rmdir(const char *path)
{
    std::string real;
    return __real_rmdir(resolve(path, real));
}

extern "C" int __wrap_unlink(const char *path)
{
    std::string real;
    return __real_unlink(resolve(path, real));
}

extern "C" int __wrap_remove(const char *path)
{
    std::string real;
    return __real_remove(resolve(path, real));
}

extern "C" int __wrap_rename(const char *from, const char *to)
{
    std::string real_from, real_to;
    return __real_rename(resolve(from, real_from), resolve(to, real_to));
}

extern "C" int __wrap_access(const char *path, int mode)
{
    std::string real;
    return __real_access(resolve(path, real), mode);
}

extern "C" DIR *__wrap_opendir(const char *path)
{
    if (refused(path))
        return nullptr;
    std::string real;
    return __real_opendir(resolve(path, real));
}

extern "C" int __wrap_sceKernelOpen(const char *path, int flags, int mode)
{
    std::string real;
    return __real_sceKernelOpen(resolve(path, real), flags, mode);
}

extern "C" int __wrap_sceKernelMkdir(const char *path, int mode)
{
    std::string real;
    return __real_sceKernelMkdir(resolve(path, real), mode);
}

extern "C" int __wrap_sceKernelRmdir(const char *path)
{
    std::string real;
    return __real_sceKernelRmdir(resolve(path, real));
}

extern "C" int __wrap_sceKernelUnlink(const char *path)
{
    std::string real;
    return __real_sceKernelUnlink(resolve(path, real));
}
#endif
