// ProsperoAI - Process-level runtime shims for the OpenGL runtime.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Adapted from ps5-homebrew-ui src/runtime/runtime_shims.c (itself from
// ps5-opengl native-app/runtime_shims.c): the never-return main policy and the
// libc entry points the clean-room libc does not provide but the statically
// linked Mesa runtime references. Unlike the kit's, the log file is opt-in.

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern int sceKernelUsleep(uint32_t microseconds);
extern uint64_t sceKernelGetProcessTime(void);

/* A diagnostic log is written only when the install folder holds dev/log.txt
 * (any content). It goes into the title's own storage; while the title runs,
 * FTP reads it at /mnt/sandbox/<TITLE_ID>_000/download0/ProsperoAI/logs/app.log. */
#define PROSPERO_LOG_SWITCH "/app0/dev/log.txt"
#define PROSPERO_DATA_DIR "/download0/ProsperoAI"
#define PROSPERO_LOG_DIR PROSPERO_DATA_DIR "/logs"
#define PROSPERO_LOG_PATH PROSPERO_LOG_DIR "/app.log"

__attribute__((constructor)) static void prospero_open_log(void)
{
    const int present = open(PROSPERO_LOG_SWITCH, O_RDONLY);
    if (present < 0)
        return;
    close(present);
    mkdir(PROSPERO_DATA_DIR, 0755);
    mkdir(PROSPERO_LOG_DIR, 0755);
    /* Keep the previous launch's log for post-close inspection. */
    rename(PROSPERO_LOG_PATH, PROSPERO_LOG_DIR "/app.prev.log");
    FILE *stream = freopen(PROSPERO_LOG_PATH, "w", stdout);
    /* Start a fresh receipt, then make both streams append-only and unbuffered
     * so the log survives a shell close or a GPU fail-stop. */
    if (stream != NULL)
        stream = freopen(PROSPERO_LOG_PATH, "a", stdout);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
    stream = freopen(PROSPERO_LOG_PATH, "a", stderr);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the shell closes the title. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[prosperoai] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

void prospero_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void prospero_glapi_tls_context_init(void)
{
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "[prosperoai] assertion failed: %s (%s:%d, %s)\n", expression, file, line,
            function);
    abort();
}

/* The SDK binds mkstemp and isatty to libScePosixForWebKit, a system library a
 * native title does not load: a call jumps to address 0. The OpenGL runtime's
 * shader cache writes through mkstemp(); these definitions are linked instead
 * of those imports. */
int mkstemps(char *template_name, int suffix_length)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned counter;
    const size_t length = template_name ? strlen(template_name) : 0;

    if (suffix_length < 0 || length < (size_t)suffix_length + 6u)
    {
        errno = EINVAL;
        return -1;
    }
    char *name = template_name + length - (size_t)suffix_length - 6u;
    for (int i = 0; i < 6; ++i)
    {
        if (name[i] != 'X')
        {
            errno = EINVAL;
            return -1;
        }
    }
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        uint64_t value = sceKernelGetProcessTime() +
                         (uint64_t)__atomic_add_fetch(&counter, 1u, __ATOMIC_RELAXED) *
                             UINT64_C(0x9E3779B97F4A7C15);
        for (int i = 0; i < 6; ++i)
        {
            name[i] = letters[value % 36u];
            value /= 36u;
        }
        const int file = open(template_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (file >= 0 || errno != EEXIST)
            return file;
    }
    errno = EEXIST;
    return -1;
}

int mkstemp(char *template_name)
{
    return mkstemps(template_name, 0);
}

int isatty(int descriptor)
{
    (void)descriptor;
    errno = ENOTTY;
    return 0;
}

/* The console's splash picture stays up until the app has presented its first
 * frame. The OpenGL runtime asks to hide it as soon as the display opens,
 * which leaves a black screen while programs build and fonts load. The build
 * routes every such call here (--wrap), and the kit's sys::hide_splash_screen()
 * lets them through by calling hui_release_splash(). */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int splash_released;

void hui_release_splash(void)
{
    splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
}

void openlog(const char *identifier, int option, int facility)
{
    (void)identifier;
    (void)option;
    (void)facility;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}
