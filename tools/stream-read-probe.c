// SPDX-License-Identifier: GPL-3.0-or-later
// S0 disk-only probe. No model allocation, inference or Vulkan upload.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (4U * 1024U * 1024U)
#define WORKERS 4

struct job {
    int fd;
    off_t offset;
    size_t bytes;
    unsigned char *buffer;
    uint64_t sample;
    int error;
};

static void *read_job(void *opaque)
{
    struct job *job = opaque;
    size_t done = 0;
    while (done < job->bytes) {
        ssize_t n = pread(job->fd, job->buffer + done, job->bytes - done, job->offset + (off_t)done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            job->error = n == 0 ? EIO : errno;
            return NULL;
        }
        done += (size_t)n;
    }
    // Touch every page; this is a repeatability sample, not an integrity hash.
    for (size_t i = 0; i < done; i += 4096) job->sample += job->buffer[i];
    if (done) job->sample += job->buffer[done - 1];
    return NULL;
}

static double now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return -1;
    return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

static int measure(int fd, uint64_t offset, uint64_t bytes, unsigned workers,
                   unsigned char *buffer, double *elapsed, uint64_t *sample)
{
    uint64_t done = 0;
    *sample = 0;
    double start = now();
    if (start < 0) return EIO;
    while (done < bytes) {
        struct job jobs[WORKERS] = {{0}};
        pthread_t threads[WORKERS];
        unsigned count = 0, launched = 0;
        int error = 0;
        while (count < workers && done < bytes) {
            size_t length = bytes - done < CHUNK ? (size_t)(bytes - done) : CHUNK;
            jobs[count] = (struct job){fd, (off_t)(offset + done), length,
                                      buffer + count * CHUNK, 0, 0};
            done += length;
            ++count;
        }
        if (workers == 1) {
            read_job(&jobs[0]);
        } else {
            for (; launched < count; ++launched) {
                error = pthread_create(&threads[launched], NULL, read_job, &jobs[launched]);
                if (error) break;
            }
            for (unsigned i = 0; i < launched; ++i) {
                int joined = pthread_join(threads[i], NULL);
                if (joined) {
                    // A failed join cannot prove the buffer is reusable. Exit the probe.
                    _exit(2);
                }
            }
            if (error) return error;
        }
        for (unsigned i = 0; i < count; ++i) {
            if (jobs[i].error) return jobs[i].error;
            *sample += jobs[i].sample;
        }
    }
    *elapsed = now() - start;
    return *elapsed > 0 ? 0 : EIO;
}

static int number(const char *text, uint64_t *value)
{
    char *end;
    if (!text[0] || text[0] == '-') return 0;
    errno = 0;
    *value = strtoull(text, &end, 10);
    return !errno && !*end;
}

int main(int argc, char **argv)
{
    const char *path;
    uint64_t offset, bytes;
    unsigned runs = 3;
    FILE *output = stdout;
#ifdef PROSPERO_PS5_STREAM_PROBE
    (void)argc;
    (void)argv;
    char config_path[1024], offset_text[32], bytes_text[32];
    FILE *config = fopen("/data/prospero-stream-probe.cfg", "r");
    if (!config) return 1;
    int fields = fscanf(config, "%1023s %31s %31s", config_path, offset_text, bytes_text);
    fclose(config);
    if (fields != 3 || !number(offset_text, &offset) || !number(bytes_text, &bytes)) return 1;
    path = config_path;
    // Task-scoped probe output only. Caller verifies this scratch path before deployment.
    output = fopen("/data/prospero-stream-probe.jsonl", "w");
    if (!output) return 1;
#else
    if (argc != 4 || !number(argv[2], &offset) || !number(argv[3], &bytes)) {
        fprintf(stderr, "usage: %s MODEL OFFSET BYTES\n", argv[0]);
        return 1;
    }
    path = argv[1];
#endif
    int fd = open(path, O_RDONLY);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) || info.st_size < 0 || !bytes ||
        offset > (uint64_t)info.st_size || bytes > (uint64_t)info.st_size - offset) {
        fprintf(output, "{\"error\":\"invalid file or extent\"}\n");
        if (fd >= 0) close(fd);
        if (output != stdout) fclose(output);
        return 1;
    }
    unsigned char *buffer = malloc(CHUNK * WORKERS);
    if (!buffer) {
        close(fd);
        if (output != stdout) fclose(output);
        return 1;
    }
    int result = 0;
    uint64_t expected = 0;
    for (unsigned run = 0; run < runs && !result; ++run) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            unsigned workers = mode ? WORKERS : 1;
            double elapsed = 0;
            uint64_t sample = 0;
            result = measure(fd, offset, bytes, workers, buffer, &elapsed, &sample);
            if (!run && !mode) expected = sample;
            if (!result && sample != expected) result = EIO;
            if (result) fprintf(output, "{\"error\":%d,\"run\":%u,\"workers\":%u}\n", result, run, workers);
            else fprintf(output,
                "{\"run\":%u,\"workers\":%u,\"offset\":%" PRIu64 ",\"bytes\":%" PRIu64
                ",\"seconds\":%.9f,\"bytes_per_second\":%.3f,\"staging_bytes\":%u,"
                "\"sample\":%" PRIu64 ",\"cache_state\":\"uncontrolled\",\"upload_measured\":false}\n",
                run, workers, offset, bytes, elapsed, (double)bytes / elapsed, CHUNK * WORKERS, sample);
            fflush(output);
            if (result) break;
        }
    }
    free(buffer);
    close(fd);
    if (output != stdout) fclose(output);
    return result ? 1 : 0;
}
