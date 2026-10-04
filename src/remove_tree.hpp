// ProsperoAI recursive removal of a directory tree through the kernel file calls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dirent.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

extern "C"
{
    int sceKernelOpen(const char *, int, int);
    int sceKernelClose(int);
    int sceKernelGetdents(int, char *, int);
    int sceKernelRmdir(const char *);
    int sceKernelUnlink(const char *);
}

inline bool prospero_remove_tree(const char *path, unsigned depth = 0)
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
                    if (sceKernelUnlink(child) != 0 && !prospero_remove_tree(child, depth + 1))
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
