// SPDX-License-Identifier: GPL-3.0-or-later
// Optional desktop operations have no equivalent in a PS5 title.
#include <errno.h>
#include <stddef.h>

int prospero_execlp(const char *file, const char *arg, ...)
{
    (void)file;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int prospero_posix_madvise(void *address, size_t length, int advice)
{
    (void)address;
    (void)length;
    (void)advice;
    // POSIX returns the error directly; mmap is disabled by the model adapter.
    return ENOSYS;
}
