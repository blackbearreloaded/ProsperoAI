// ProsperoAI - A newer version: asking the catalog, and installing it in place.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "update.hpp"

#include "self_update.h"
#include "storage.hpp"
#include "update_check.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>

// A development build takes the offer from update-offer.txt in the app's folder instead
// of the catalog (UPDATE_DEV_OFFER=1). It skips the catalog's signature: never shipped.
#ifndef PROSPERO_UPDATE_DEV_OFFER
#define PROSPERO_UPDATE_DEV_OFFER 0
#endif

extern "C" int scePthreadCreate(void **, const void *, void *(*)(void *), void *, const char *);
extern "C" int scePthreadDetach(void *);
extern "C" int sceKernelDebugOutText(int, const char *);

// The kit's paths (third_party/update-check/self_update_paths.h): with filesystem access
// /app0 and /download0 are not where the app's files are.
extern "C" const char *prosperoai_self_update_path(int which)
{
    static const std::string helper = prospero::storage().app_dir + "/self-updater.elf";
    static const std::string param = prospero::storage().app_dir + "/sce_sys/param.json";
    static const std::string sequence = prospero::storage().data_root + "/self-update-sequence";
    return which == 0 ? helper.c_str() : which == 1 ? param.c_str() : sequence.c_str();
}

namespace prospero::update
{
namespace
{
std::mutex g_lock;
self_update_check_result g_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_offer{};
self_update_job g_job{}; // zero until the first begin, as the kit asks
bool g_answered = false, g_taken = false, g_begun = false;
std::atomic<bool> g_asked{false};

// For the console's log, and with it the debug log (vulkan/debug_tee.cpp).
void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
void say(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    std::strcat(line, "\n");
    sceKernelDebugOutText(0, line);
}

#if PROSPERO_UPDATE_DEV_OFFER
// Development only: five lines (the new content version, the release's name, its ZIP on
// GitHub, its SHA-256, its size in bytes); any further lines are the release notes.
bool development_offer(self_update_offer *out)
{
    const std::string path = storage().app_dir + "/update-offer.txt";
    std::FILE *file = std::fopen(path.c_str(), "r");
    if (!file)
        return false;
    char lines[5][600] = {};
    bool complete = true;
    for (auto &line : lines)
    {
        if (!std::fgets(line, sizeof(line), file))
        {
            complete = false;
            break;
        }
        line[std::strcspn(line, "\r\n")] = '\0';
    }
    std::string notes;
    char more[600];
    while (complete && std::fgets(more, sizeof(more), file))
    {
        more[std::strcspn(more, "\r\n")] = '\0';
        notes += (notes.empty() ? "" : "\n") + std::string(more);
    }
    std::fclose(file);
    static self_update_offer filled;
    filled = self_update_offer{};
    std::snprintf(filled.notes, sizeof(filled.notes), "%s", notes.c_str());
    if (!complete ||
        !update_check_read_param(prosperoai_self_update_path(1), filled.title, filled.installed))
        return false;
    std::snprintf(filled.name, sizeof(filled.name), "ProsperoAI");
    std::snprintf(filled.available, sizeof(filled.available), "%s", lines[0]);
    std::snprintf(filled.version, sizeof(filled.version), "%s", lines[1]);
    std::snprintf(filled.artifact, sizeof(filled.artifact), "%s", lines[2]);
    std::snprintf(filled.sha256, sizeof(filled.sha256), "%s", lines[3]);
    filled.size = std::strtoull(lines[4], nullptr, 10);
    // Like the catalog, only a newer version is offered (content versions compare as text).
    if (std::strcmp(filled.available, filled.installed) <= 0)
        return false;
    *out = filled;
    return true;
}
#endif

// Blocks on the network for as long as it takes: a thread of its own.
void *run_check(void *)
{
    static const char *const kNames[] = {"available", "up-to-date", "unknown", "untrusted",
                                         "not-installable"};
    static self_update_offer found; // 17 KB: not for this thread's stack
    found = self_update_offer{};
    self_update_check_result state = self_update_check_self(&found);
#if PROSPERO_UPDATE_DEV_OFFER
    if (development_offer(&found))
    {
        say("[prosperoai] update check: DEVELOPMENT: update-offer.txt replaces the catalog's "
            "answer");
        state = SELF_UPDATE_AVAILABLE;
    }
#endif
    say("[prosperoai] update check: result=%s installed=%s available=%s version=%s size=%llu",
        static_cast<unsigned>(state) < 5 ? kNames[state] : "?",
        found.installed[0] ? found.installed : "-", found.available[0] ? found.available : "-",
        found.version[0] ? found.version : "-", static_cast<unsigned long long>(found.size));
    const std::lock_guard<std::mutex> guard(g_lock);
    g_answer = state;
    g_offer = found;
    g_answered = true;
    return nullptr;
}
} // namespace

void check()
{
    if (g_asked.exchange(true))
        return;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return;
    void *thread = nullptr;
    if (pthread_attr_setstacksize(&attributes, 1024 * 1024) == 0 &&
        scePthreadCreate(&thread, &attributes, run_check, nullptr, "prospero-update") == 0)
        scePthreadDetach(thread);
    else
        say("[prosperoai] update check: the thread could not start");
    pthread_attr_destroy(&attributes);
}

bool take_offer(Offer *offer)
{
    const std::lock_guard<std::mutex> guard(g_lock);
    if (!g_answered || g_taken)
        return false;
    g_taken = true;
    if (g_answer != SELF_UPDATE_AVAILABLE && g_answer != SELF_UPDATE_NOT_INSTALLABLE)
        return false;
    offer->installable = g_answer == SELF_UPDATE_AVAILABLE;
    std::snprintf(offer->version, sizeof(offer->version), "%s",
                  g_offer.version[0] ? g_offer.version : g_offer.available);
    offer->size = g_offer.size;
    offer->notes = g_offer.notes;
    offer->notes_truncated = g_offer.notes_truncated != 0;
    return true;
}

bool begin()
{
    const std::lock_guard<std::mutex> guard(g_lock);
    if (g_answer != SELF_UPDATE_AVAILABLE)
        return false;
    if (g_begun)
        self_update_finish(&g_job);
    g_begun = self_update_start(&g_job, self_update_console(), &g_offer) == 1;
    say("[prosperoai] update: %s %s", g_begun ? "updating to" : "could not begin for",
        g_offer.available);
    return g_begun;
}

void poll(Progress *progress)
{
    static int reported = -1;
    const std::lock_guard<std::mutex> guard(g_lock);
    *progress = Progress{};
    if (!g_begun)
        return;
    self_update_status status{};
    self_update_poll(&g_job, &status);
    static constexpr Phase kPhases[] = {Phase::idle,      Phase::starting, Phase::downloading,
                                        Phase::unpacking, Phase::ready,    Phase::applying,
                                        Phase::cancelled, Phase::failed};
    progress->phase = static_cast<unsigned>(status.phase) < 8 ? kPhases[status.phase] : Phase::idle;
    progress->done = status.done;
    progress->total = status.total;
    std::snprintf(progress->time_left, sizeof(progress->time_left), "%s", status.time_left);
    std::snprintf(progress->error, sizeof(progress->error), "%s", status.error);
    if (static_cast<int>(status.phase) != reported)
    {
        reported = static_cast<int>(status.phase);
        say("[prosperoai] update: phase=%d done=%llu total=%llu error=%s", reported,
            static_cast<unsigned long long>(status.done),
            static_cast<unsigned long long>(status.total), status.error[0] ? status.error : "-");
    }
}

void cancel()
{
    const std::lock_guard<std::mutex> guard(g_lock);
    if (g_begun)
        self_update_cancel(&g_job);
}

bool apply()
{
    const std::lock_guard<std::mutex> guard(g_lock);
    if (!g_begun)
        return false;
    const bool going = self_update_apply(&g_job) == 1;
    say("[prosperoai] update: %s",
        going ? "staged; the helper replaces the files once ProsperoAI has closed"
              : "the helper did not take the go-ahead");
    return going;
}

void finish()
{
    const std::lock_guard<std::mutex> guard(g_lock);
    if (!g_begun)
        return;
    self_update_finish(&g_job);
    g_begun = false;
}
} // namespace prospero::update
