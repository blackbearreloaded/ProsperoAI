/*
 * ProsperoAI - HTTPS downloads, read a piece at a time.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "https_get.h"

#include "console_curl.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* What may wait between libcurl and the reader. A chunk that does not fit pauses the
 * transfer until the reader has caught up, so a slow disk holds the network back
 * instead of filling memory. */
#define HTTPS_BUFFER_BYTES (1024u * 1024u)

struct prospero_https
{
    CURLM *multi;
    CURL *easy;
    char *data;
    size_t have, taken;
    int done, paused;
    CURLcode result;
    prospero_https_stop stop;
    void *stop_user;
    char error[CURL_ERROR_SIZE];
};

static pthread_once_t global_once = PTHREAD_ONCE_INIT;
static CURLcode global_result;
static int receive_buffer = -1; /* what the last socket was given */

static void global_start(void)
{
    global_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

/* The console gives a socket a 64 KB receive buffer. A sender may have that much on
 * its way and no more, so a server 60 ms away delivers about 1 MB/s whatever the line
 * could carry (measured in ProsperoTV: 1.07 MB/s where a PC on the same network got
 * 18 MB/s). The buffer is asked for before the connection is made, when the window's
 * scale is agreed: the largest size the console accepts. */
static int on_socket(void *user, curl_socket_t socket, curlsocktype purpose)
{
    (void)user;
    (void)purpose;
    (void)console_curl_nonblocking(socket); /* as console_curl_setup() does */
    int granted = 0;
    for (int bytes = 4 * 1024 * 1024; bytes >= 256 * 1024; bytes /= 2)
        if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0)
        {
            granted = bytes;
            break;
        }
    receive_buffer = granted;
    return CURL_SOCKOPT_OK;
}

int prospero_https_receive_buffer(void)
{
    return receive_buffer;
}

static size_t on_data(char *bytes, size_t size, size_t count, void *user)
{
    prospero_https *session = user;
    const size_t length = size * count;
    if (session->taken == session->have)
        session->have = session->taken = 0;
    if (session->have + length > HTTPS_BUFFER_BYTES)
    {
        if (session->taken != 0)
        {
            memmove(session->data, session->data + session->taken, session->have - session->taken);
            session->have -= session->taken;
            session->taken = 0;
        }
        if (session->have + length > HTTPS_BUFFER_BYTES)
        {
            session->paused = 1;
            return CURL_WRITEFUNC_PAUSE;
        }
    }
    memcpy(session->data + session->have, bytes, length);
    session->have += length;
    return length;
}

/* True once the owner wants the request to end; the request then counts as failed. */
static int stopped(prospero_https *session)
{
    if (!session->stop || !session->stop(session->stop_user))
        return 0;
    session->done = 1;
    session->result = CURLE_ABORTED_BY_CALLBACK;
    snprintf(session->error, sizeof(session->error), "stopped");
    return 1;
}

static void drop_request(prospero_https *session)
{
    if (!session->easy)
        return;
    curl_multi_remove_handle(session->multi, session->easy);
    curl_easy_cleanup(session->easy);
    session->easy = NULL;
}

/* One step of the transfer: what is ready is moved, and the result is kept when it ends. */
static void pump(prospero_https *session)
{
    int running = 0;
    if (session->paused && session->have == session->taken)
    {
        session->have = session->taken = 0;
        session->paused = 0;
        curl_easy_pause(session->easy, CURLPAUSE_CONT);
    }
    CURLMcode code = curl_multi_perform(session->multi, &running);
    if (code == CURLM_OK && running && session->have == session->taken && !session->paused)
        code = curl_multi_poll(session->multi, NULL, 0, 250, NULL);
    if (code != CURLM_OK)
    {
        snprintf(session->error, sizeof(session->error), "%s", curl_multi_strerror(code));
        session->result = CURLE_RECV_ERROR;
        session->done = 1;
        return;
    }
    int left = 0;
    for (CURLMsg *message; (message = curl_multi_info_read(session->multi, &left)) != NULL;)
        if (message->msg == CURLMSG_DONE && message->easy_handle == session->easy)
        {
            session->result = message->data.result;
            session->done = 1;
            if (session->result != CURLE_OK && !session->error[0])
                snprintf(session->error, sizeof(session->error), "%s",
                         curl_easy_strerror(session->result));
        }
}

prospero_https *prospero_https_open(void)
{
    pthread_once(&global_once, global_start);
    if (global_result != CURLE_OK)
        return NULL;
    prospero_https *session = calloc(1, sizeof(*session));
    if (!session)
        return NULL;
    session->multi = curl_multi_init();
    session->data = malloc(HTTPS_BUFFER_BYTES);
    if (!session->multi || !session->data)
    {
        prospero_https_close(session);
        return NULL;
    }
    return session;
}

void prospero_https_set_stop(prospero_https *session, prospero_https_stop stop, void *user)
{
    session->stop = stop;
    session->stop_user = user;
}

void prospero_https_close(prospero_https *session)
{
    if (!session)
        return;
    drop_request(session);
    if (session->multi)
        curl_multi_cleanup(session->multi);
    free(session->data);
    free(session);
}

int prospero_https_get(prospero_https *session, const char *url, long *status)
{
    return prospero_https_get_from(session, url, 0, status);
}

int prospero_https_get_from(prospero_https *session, const char *url, unsigned long long offset,
                            long *status)
{
    drop_request(session);
    session->have = session->taken = 0;
    session->done = session->paused = 0;
    session->result = CURLE_OK;
    session->error[0] = '\0';
    session->easy = curl_easy_init();
    if (!session->easy)
    {
        snprintf(session->error, sizeof(session->error), "libcurl could not make a request");
        return 1;
    }
    CURL *easy = session->easy;
    console_curl_setup(easy);
    curl_easy_setopt(easy, CURLOPT_SOCKOPTFUNCTION, on_socket);
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "ProsperoAI/1.0");
    curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 30L);
    /* A transfer that moves nothing for a minute has stalled. */
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 256L * 1024L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, session);
    curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, session->error);
    /* CURLOPT_RANGE rather than RESUME_FROM: a server that ignores the range then answers
     * 200 with the whole body, which the caller can still use, instead of an error. */
    char range[32];
    if (offset)
    {
        snprintf(range, sizeof(range), "%llu-", offset);
        curl_easy_setopt(easy, CURLOPT_RANGE, range);
    }
    if (curl_multi_add_handle(session->multi, easy) != CURLM_OK)
    {
        snprintf(session->error, sizeof(session->error), "libcurl could not start the request");
        return 1;
    }
    while (!session->done && session->have == session->taken && !session->paused)
    {
        if (stopped(session))
            return 1;
        pump(session);
    }
    if (session->done && session->result != CURLE_OK)
        return 1;
    long code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
    if (status)
        *status = code;
    return 0;
}

int prospero_https_read(prospero_https *session, void *buffer, size_t size)
{
    if (!session->easy || stopped(session))
        return -1;
    while (session->have == session->taken && !session->done)
    {
        pump(session);
        if (stopped(session))
            return -1;
    }
    size_t ready = session->have - session->taken;
    if (ready == 0)
        return session->result == CURLE_OK ? 0 : -1;
    if (ready > size)
        ready = size;
    if (ready > 0x40000000u)
        ready = 0x40000000u;
    memcpy(buffer, session->data + session->taken, ready);
    session->taken += ready;
    return (int)ready;
}

const char *prospero_https_error(const prospero_https *session)
{
    return session && session->error[0] ? session->error : "no answer";
}
