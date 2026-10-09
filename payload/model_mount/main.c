// Shared model directory mount for ProsperoAI test titles.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include "model_paths.hpp"
#ifndef PROSPERO_MOUNT_TITLE
#define PROSPERO_MOUNT_TITLE "PPSA99023"
#endif
extern unsigned sceLncUtilGetAppIdOfRunningBigApp(void);
extern int sceLncUtilGetAppTitleId(unsigned, char *);
static void report(const char *action, int result, int error)
{
    FILE *file = fopen("/data/prospero-model-mount.log", "a");
    if (file) { fprintf(file, "%s pid=%d title=%s rc=%d errno=%d\n", action, getpid(), PROSPERO_MOUNT_TITLE, result, error); fclose(file); }
}
static int mount_models(const char *path)
{
    char directory[256];
    snprintf(directory, sizeof(directory), "%s", path);
    // Create only directories below this running title's sandbox.
    for (char *p = directory + strlen("/mnt/sandbox/"); *p; ++p)
        if (*p == '/') { *p = 0; if (mkdir(directory, 0777) && errno != EEXIST) return -1; *p = '/'; }
    if (mkdir(directory, 0777) && errno != EEXIST) return -1;
    struct statfs fs;
    if (statfs(path, &fs) == 0 && strcmp(fs.f_fstypename, "nullfs") == 0 &&
        strcmp(fs.f_mntonname, path) == 0) return 0;
    const char *arguments[] = {"fstype", "nullfs", "fspath", path, "target", PROSPERO_MODEL_ROOT};
    struct iovec vector[6];
    for (unsigned i = 0; i < 6; ++i) { vector[i].iov_base = (void *)arguments[i]; vector[i].iov_len = strlen(arguments[i]) + 1; }
    return nmount(vector, 6, 0);
}
static int find_target(char *target, size_t capacity)
{
    DIR *directory = opendir("/mnt/sandbox");
    if (!directory) return -1;
    int found = 0;
    for (struct dirent *entry; (entry = readdir(directory)) != NULL;)
    {
        if (strncmp(entry->d_name, PROSPERO_MOUNT_TITLE "_", sizeof(PROSPERO_MOUNT_TITLE)) != 0) continue;
        const char *suffix = entry->d_name + sizeof(PROSPERO_MOUNT_TITLE);
        if (strlen(suffix) != 3 || strspn(suffix, "0123456789") != 3) continue;
        char executable[256];
        snprintf(executable, sizeof(executable), "/mnt/sandbox/%s/app0/eboot.bin", entry->d_name);
        struct stat binary;
        // access(F_OK) returns EACCES on this firmware even when stat/open work.
        if (stat(executable, &binary) != 0 || !S_ISREG(binary.st_mode)) continue;
        snprintf(target, capacity, "/mnt/sandbox/%s%s", entry->d_name, PROSPERO_MODEL_ROOT);
        found = 1;
        break;
    }
    closedir(directory);
    return found ? 0 : -1;
}
static void remove_mount_directories(const char *target)
{
    char directory[256];
    snprintf(directory, sizeof(directory), "%s", target);
    // Remove only empty directories created for this mount. Never remove model files.
    for (unsigned i = 0; i < 4; ++i)
    {
        if (rmdir(directory) != 0) break;
        char *separator = strrchr(directory, '/');
        if (!separator) break;
        *separator = 0;
    }
}
int main(void)
{
    char target[256] = {0};
    int mounted = 0, last_error = 0;
    unsigned application = 0xffffffffu;
    report("resident", 0, 0);
    for (;;)
    {
        struct stat stop;
        if (stat("/data/prospero-model-mount.stop", &stop) == 0)
        {
            if (!mounted || unmount(target, 0) == 0)
            {
                if (mounted) remove_mount_directories(target);
                unlink("/data/prospero-model-mount.stop");
                report("stopped", 0, 0);
                return 0;
            }
        }
        char title[32] = {0};
        unsigned active = sceLncUtilGetAppIdOfRunningBigApp();
        int matching = active != 0xffffffffu && active != 0 && sceLncUtilGetAppTitleId(active, title) == 0 && strcmp(title, PROSPERO_MOUNT_TITLE) == 0;
        if (mounted && (!matching || active != application))
        {
            int result = unmount(target, 0);
            if (result == 0 || errno == ENOENT || errno == EINVAL)
            {
                mounted = 0;
                report("unmount", result, result ? errno : 0);
                remove_mount_directories(target);
            }
        }
        if (matching && !mounted)
        {
            struct stat source;
            if (stat(PROSPERO_MODEL_ROOT, &source) == 0 && S_ISDIR(source.st_mode) && find_target(target, sizeof(target)) == 0)
            {
                int result = mount_models(target);
                int error = result ? errno : 0;
                if (!result || error != last_error) report("mount", result, error);
                last_error = error;
                if (!result) { mounted = 1; application = active; }
            }
        }
        usleep(50000);
    }
}
