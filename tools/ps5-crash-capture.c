// SPDX-License-Identifier: GPL-3.0-or-later
// Preserve only the RADV test's short-lived crash files. Never removes sources.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned long long copied;
static char buffer[65536];
static time_t deadline;

static void copy_tree(const char *source, const char *target, int depth)
{
    if (depth > 5 || copied >= 128ULL * 1024 * 1024 || time(NULL) >= deadline)
        return;
    struct stat st;
    if (lstat(source, &st) != 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        mkdir(target, 0777);
        DIR *dir = opendir(source);
        if (!dir) return;
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char src[1024], dst[1024];
            if (snprintf(src, sizeof(src), "%s/%s", source, entry->d_name) >= (int)sizeof(src) ||
                snprintf(dst, sizeof(dst), "%s/%s", target, entry->d_name) >= (int)sizeof(dst)) continue;
            copy_tree(src, dst, depth + 1);
        }
        closedir(dir);
    } else if (S_ISREG(st.st_mode)) {
        int input = open(source, O_RDONLY);
        if (input < 0) return;
        int output = open(target, O_WRONLY | O_CREAT, 0666);
        if (output < 0) { close(input); return; }
        off_t offset = lseek(output, 0, SEEK_END);
        if (offset < 0 || lseek(input, offset, SEEK_SET) < 0) {
            close(input); close(output); return;
        }
        ssize_t count;
        while (copied < 128ULL * 1024 * 1024 && time(NULL) < deadline) {
            unsigned long long remaining = 128ULL * 1024 * 1024 - copied;
            count = read(input, buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            ssize_t done = 0;
            while (done < count) {
                ssize_t n = write(output, buffer + done, count - done);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                done += n;
                copied += n;
            }
            if (done != count) break;
        }
        close(input); close(output);
    }
}

int main(void)
{
    const char *root = "/devlog/system/sce_coredumps.0";
    char destination[128];
    snprintf(destination, sizeof(destination), "/data/prospero-crash-%lld", (long long)time(NULL));
    if (mkdir(destination, 0777) != 0) return 1;
    FILE *log = fopen("/data/prospero-crash-capture.log", "w");
    if (log) { fprintf(log, "watching PPSA99023 for 120 seconds; destination=%s\n", destination); fflush(log); }
    deadline = time(NULL) + 120;
    while (time(NULL) < deadline && copied < 128ULL * 1024 * 1024) {
        DIR *dir = opendir(root);
        if (dir) {
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                if (strncmp(entry->d_name, "PPSA99023_", 10)) continue;
                char src[1024], dst[1024];
                snprintf(src, sizeof(src), "%s/%s", root, entry->d_name);
                snprintf(dst, sizeof(dst), "%s/%s", destination, entry->d_name);
                copy_tree(src, dst, 0);
            }
            closedir(dir);
        }
        usleep(1000);
    }
    if (log) { fprintf(log, "finished: copied=%llu bytes\n", copied); fclose(log); }
    return 0;
}
