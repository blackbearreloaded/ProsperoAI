// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <unistd.h>
#include <vector>

// Like AGC's queued loader, keep multiple disk reads in flight. pread avoids
// sharing the FILE cursor between workers; every worker finishes before return,
// including on errors, so the upload buffer can always be safely reused.
inline bool prospero_ps5_read_parallel(FILE * file, void * destination, size_t size) {
    constexpr size_t chunk = 2 * 1024 * 1024;
    constexpr size_t workers = 4;
    if (std::getenv("PROSPERO_MODEL_IO_SERIAL") || size < chunk) return false;
    const off_t start = ftello(file);
    const int fd = fileno(file);
    if (start < 0 || fd < 0) return false;
    for (size_t window = 0; window < size;) {
        std::vector<std::future<bool>> pending;
        size_t bytes = 0;
        for (size_t i = 0; i < workers && window + bytes < size; ++i) {
            const size_t offset = window + bytes;
            const size_t count = std::min(chunk, size - offset);
            pending.emplace_back(std::async(std::launch::async, [=] {
                size_t done = 0;
                while (done < count) {
                    const ssize_t n = pread(fd, static_cast<char *>(destination) + offset + done,
                                            count - done, start + offset + done);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) return false;
                    done += static_cast<size_t>(n);
                }
                return true;
            }));
            bytes += count;
        }
        bool ok = true;
        for (auto & result : pending) ok = result.get() && ok;
        // The cursor is untouched on failure; caller can retry via fread.
        if (!ok) return false;
        window += bytes;
    }
    return fseeko(file, start + size, SEEK_SET) == 0;
}
