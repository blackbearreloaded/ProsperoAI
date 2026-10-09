/*
 * ProsperoAI - HTTPS downloads, read a piece at a time.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The model downloader's transport, on libcurl. The console's own HTTPS library
 * refuses every public site once the app has filesystem access (it no longer finds
 * the certificate authorities), so the app brings libcurl and OpenSSL, as the other
 * Prospero apps do (console_curl.c). Redirects are followed, to https only.
 */
#ifndef PROSPERO_HTTPS_GET_H
#define PROSPERO_HTTPS_GET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct prospero_https prospero_https;

    /* One connection pool for a series of requests; NULL when libcurl could not start. */
    prospero_https *prospero_https_open(void);
    void prospero_https_close(prospero_https *session);

    /* Starts a GET and waits for the answer's first bytes (or its end). 0 and the
     * HTTP status of the last answer on success; nonzero when the request failed. */
    int prospero_https_get(prospero_https *session, const char *url, long *status);

    /* The next bytes of the body: their count, 0 at its end, -1 on failure. */
    int prospero_https_read(prospero_https *session, void *buffer, size_t size);

    /* What went wrong, in libcurl's words; valid until the next request. */
    const char *prospero_https_error(const prospero_https *session);

    /* The receive buffer the last socket was given, in bytes: 0 when the console kept
     * its default (64 KB, which holds a distant server to about 1 MB/s), -1 before the
     * first connection. */
    int prospero_https_receive_buffer(void);

#ifdef __cplusplus
}
#endif

#endif /* PROSPERO_HTTPS_GET_H */
