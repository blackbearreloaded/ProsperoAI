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

    /* One connection pool for a series of requests; NULL when libcurl could not start.
     * The session owns a receive buffer of 4 MB, which suits a model download. */
    prospero_https *prospero_https_open(void);

    /* The same with a receive buffer of `bytes` (clamped to 16 KB ... 8 MB). Search
     * providers answer in kilobytes and are read as they arrive, so they open a
     * small session instead of borrowing megabytes from a loaded model. */
    prospero_https *prospero_https_open_sized(size_t bytes);

    /* Request headers for the next and following requests on this session.
     * `prospero_https_add_header` takes one "Name: value" line; an empty value
     * ("Name:") removes a header libcurl would send by default. Some search
     * engines answer differently, or only, when asked like a browser. */
    int prospero_https_add_header(prospero_https *session, const char *line);
    void prospero_https_clear_headers(prospero_https *session);

    /* Overrides the user agent for this session. NULL restores ProsperoAI/1.0. */
    void prospero_https_set_user_agent(prospero_https *session, const char *user_agent);

    /* Keeps cookies for the lifetime of the session, in memory and nowhere else.
     * An engine that hands out a token before it answers needs this. */
    void prospero_https_enable_cookies(prospero_https *session);

    /* Asks for compressed transfers (identity when libcurl has no codec). This
     * cuts what a text answer costs on the wire and in the buffer. */
    void prospero_https_set_compression(prospero_https *session, int enabled);

    /* A POST with a body, for the engines that take one. Same reading API. */
    int prospero_https_post(prospero_https *session, const char *url, const char *body,
                            const char *content_type, long *status);
    void prospero_https_close(prospero_https *session);

    /* Starts a GET and waits for the answer's first bytes (or its end). 0 and the
     * HTTP status of the last answer on success; nonzero when the request failed. */
    int prospero_https_get(prospero_https *session, const char *url, long *status);

    /* The same, asking only for the bytes from `offset` on (an HTTP Range request): a
     * server that honours it answers 206, one that does not sends the whole body with 200. */
    int prospero_https_get_from(prospero_https *session, const char *url,
                                unsigned long long offset, long *status);

    /* Requests one inclusive byte range. The caller must require HTTP 206. */
    int prospero_https_get_range(prospero_https *session, const char *url,
                                 unsigned long long first, unsigned long long last,
                                 long *status);

    /* The next bytes of the body: their count, 0 at its end, -1 on failure. */
    int prospero_https_read(prospero_https *session, void *buffer, size_t size);

    /* Asked while a request waits for the network, about four times a second: a nonzero
     * answer ends the request, and prospero_https_get/read report a failure. */
    typedef int (*prospero_https_stop)(void *user);
    void prospero_https_set_stop(prospero_https *session, prospero_https_stop stop, void *user);

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
