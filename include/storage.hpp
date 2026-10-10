// ProsperoAI - Filesystem access and where the app keeps its files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace prospero
{
// With filesystem access (Lapy, vulkan/elevation) the app uses real console paths:
// models, conversations, settings and logs live in /data/prosperoai, where they
// outlast the app and can be reached over FTP, and the app's own folder is wherever
// the console mounted it from. Without it (no resident Lapy service and no payload
// loader, or the request was refused) everything stays in the app's sandbox.
//
// The sources, and the prebuilt model runtimes, name files by their sandbox paths
// (/app0/..., /download0/...). vulkan/storage_paths.cpp turns such a path into the
// real one at the moment a file is opened, so those names are valid either way.
struct Storage
{
    int access = -1;            // 0 granted, otherwise the elevation status that refused it
    const char *route = "none"; // existing, resident, helper or none
    std::string title_id;       // from the packaged param.json
    std::string version;        // its contentVersion
    std::string app_dir;        // eboot.bin, assets/, sce_sys/
    std::string data_root;      // prosperoai.cfg, sessions/, logs/, work/
    std::string model_root;     // one folder or .gguf file per model
    unsigned moved_models = 0;  // model folders brought over from an earlier version's place
    bool granted() const
    {
        return access == 0;
    }
};

// Requests filesystem access and settles every path. Call it first thing in main,
// before any thread exists: Lapy accepts only a single-threaded process.
const Storage &prepare_storage();

// The paths prepare_storage() settled.
const Storage &storage();

// What a sandbox path is on the console now; the path itself when it is not one.
std::string real_path(const char *path);

// True for a path under /data while /data is only a folder of the app's own sandbox.
// ProsperoAI 01.001.000 started a helper that stays on the console until it restarts
// and mounts the old model folder into the sandbox at every start, under a /data it
// makes there. That folder takes files, so it would pass for filesystem access, and
// everything written to it is lost with the sandbox. Such paths are refused, which
// sends the request on to Lapy.
bool private_data_path(const char *path);
} // namespace prospero
