// ProsperoAI - Embedded HTTP server: Ollama-compatible API and web chat UI.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal hand-rolled HTTP/1.1 server running on its own thread so an
// external PC or phone browser can drive the same on-device model runtime
// the RmlUi chat UI uses.
//
// Network calls go through the ps5-payload-sdk libSceNet import stubs
// (linked as a normal NEEDED module), the same way the SDK's http2_get sample
// does. No runtime module lookup or sceKernelDlsym() is involved.

#include "http_server.hpp"
#include "model_paths.hpp"

#include "gpt_runtime.hpp"
#if defined(PS5_LLAMA_VULKAN) && defined(PROSPERO_UI_VULKAN)
#include "model_downloader_ps5.hpp"
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <cctype>
#include <stdexcept>
#include <memory>
#include <cmath>
#include <mutex>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

extern "C"
{
    int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                         void *argument, const char *name);
    int scePthreadJoin(void *thread, void **result);
    int sceNetInit(void);
    int sceNetPoolCreate(const char *name, int size, int flags);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSocketClose(int sock);
    int sceNetConnect(int sock, const void *address, unsigned int address_length);
    int sceNetBind(int sock, const void *address, unsigned int address_length);
    int sceNetListen(int sock, int backlog);
    int sceNetAccept(int sock, void *address, unsigned int *address_length);
    int sceNetSend(int sock, const void *buffer, unsigned long length, int flags);
    int sceNetRecv(int sock, void *buffer, unsigned long length, int flags);
    int sceNetSetsockopt(int sock, int level, int option, const void *value, unsigned int length);
}

namespace
{

char last_net_detail[96] = "";
std::mutex last_status_mutex;
char last_status[96] = "";

void log_line(const char *text)
{
    if (std::strncmp(text, "NET:", 4) == 0)
        std::snprintf(last_net_detail, sizeof(last_net_detail), "%s", text);
    std::lock_guard<std::mutex> lock(last_status_mutex);
    std::snprintf(last_status, sizeof(last_status), "%s", text);
}

void log_result(const char *step, int result)
{
    char line[96];
    std::snprintf(line, sizeof(line), "NET: %s=%d", step, result);
    log_line(line);
}

// FreeBSD socket ABI constants (Orbis' kernel is FreeBSD-derived; these
// values are stable, publicly documented BSD constants, not Sony-specific).
constexpr int kAfInet = 2;
constexpr int kSockStream = 1;
constexpr int kSolSocket = 0xffff;
constexpr int kSoReuseAddr = 0x0004;

constexpr int kNetPoolSize = 4 * 1024 * 1024;
constexpr int kListenBacklog = 4;
constexpr std::size_t kRequestCapacity = 256 * 1024;
constexpr std::size_t kResponseTextCapacity = 4096;
constexpr std::size_t kResponseCapacity = 8192;
unsigned short kHttpServerPort = 11434;

// FreeBSD sockaddr_in layout: 1-byte length, 1-byte family, big-endian
// port/address, 8 bytes of padding. Deliberately not reusing a system
// <netinet/in.h> definition: this runtime shim does not vend one.
struct SceNetSockaddrIn
{
    std::uint8_t sin_len;
    std::uint8_t sin_family;
    std::uint16_t sin_port;
    std::uint32_t sin_addr;
    std::uint8_t sin_zero[8];
};

// Dynamically resolved libSceNet entry points.
struct NetApi
{
    int (*init)();
    int (*pool_create)(const char *, int, int);
    int (*socket)(const char *, int, int, int);
    int (*socket_close)(int);
    int (*connect)(int, const void *, unsigned int);
    int (*bind)(int, const void *, unsigned int);
    int (*listen)(int, int);
    int (*accept)(int, void *, unsigned int *);
    int (*send)(int, const void *, unsigned long, int);
    int (*recv)(int, void *, unsigned long, int);
    int (*setsockopt)(int, int, int, const void *, unsigned int);
};

bool bring_up_net_api(NetApi *api)
{
    api->init = sceNetInit;
    api->pool_create = sceNetPoolCreate;
    api->socket = sceNetSocket;
    api->socket_close = sceNetSocketClose;
    api->connect = sceNetConnect;
    api->bind = sceNetBind;
    api->listen = sceNetListen;
    api->accept = sceNetAccept;
    api->send = sceNetSend;
    api->recv = sceNetRecv;
    api->setsockopt = sceNetSetsockopt;

    static std::once_flag once;
    static bool ready = false;
    std::call_once(once,
                   [&]
                   {
                       const int init = api->init();
                       log_result("sceNetInit", init);
                       if (init < 0)
                           return;
                       const int pool = api->pool_create("prosperoai-http", kNetPoolSize, 0);
                       log_result("sceNetPoolCreate", pool);
                       ready = pool >= 0;
                   });
    return ready;
}

pthread_mutex_t generation_mutex = PTHREAD_MUTEX_INITIALIZER;

// --- tiny JSON helpers -----------------------------------------------

const char *json_value_start(const char *body, const char *key)
{
    char needle[64];
    std::snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *at = std::strstr(body, needle);
    if (at == nullptr)
        return nullptr;
    at = std::strchr(at + std::strlen(needle), ':');
    if (at == nullptr)
        return nullptr;
    ++at;
    while (*at == ' ' || *at == '\t' || *at == '\n' || *at == '\r')
        ++at;
    return at;
}

bool json_copy_string(const char *at, char *output, std::size_t capacity)
{
    if (at == nullptr || *at != '"' || capacity == 0)
        return false;
    ++at;
    std::size_t written = 0;
    while (*at != '\0' && *at != '"' && written + 1 < capacity)
    {
        if (*at == '\\' && at[1] != '\0')
        {
            ++at;
            char decoded = *at;
            switch (*at)
            {
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            default:
                decoded = *at;
                break;
            }
            output[written++] = decoded;
            ++at;
        }
        else
        {
            output[written++] = *at++;
        }
    }
    output[written] = '\0';
    return true;
}

bool json_find_string(const char *body, const char *key, char *output, std::size_t capacity)
{
    return json_copy_string(json_value_start(body, key), output, capacity);
}

void json_append_escaped(char *output, std::size_t capacity, const char *text)
{
    std::size_t written = std::strlen(output);
    for (; *text != '\0' && written + 2 < capacity; ++text)
    {
        const char c = *text;
        const char *escape = nullptr;
        switch (c)
        {
        case '"':
            escape = "\\\"";
            break;
        case '\\':
            escape = "\\\\";
            break;
        case '\n':
            escape = "\\n";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\t':
            escape = "\\t";
            break;
        default:
            break;
        }
        if (escape != nullptr)
        {
            const std::size_t escape_length = std::strlen(escape);
            if (written + escape_length + 1 >= capacity)
                break;
            std::memcpy(output + written, escape, escape_length);
            written += escape_length;
        }
        else if (static_cast<unsigned char>(c) >= 0x20)
        {
            output[written++] = c;
        }
    }
    output[written] = '\0';
}

bool json_find_message(const char *messages_array, unsigned index, char *role,
                       std::size_t role_capacity, char *content, std::size_t content_capacity)
{
    const char *cursor = messages_array;
    for (unsigned seen = 0; seen <= index; ++seen)
    {
        cursor = std::strchr(cursor, '{');
        if (cursor == nullptr)
            return false;
        if (seen < index)
        {
            cursor = std::strchr(cursor, '}');
            if (cursor == nullptr)
                return false;
            ++cursor;
        }
    }
    const char *object_end = std::strchr(cursor, '}');
    if (object_end == nullptr)
        return false;
    char object[2048];
    const std::size_t length = static_cast<std::size_t>(object_end - cursor) + 1;
    if (length >= sizeof(object))
        return false;
    std::memcpy(object, cursor, length);
    object[length] = '\0';
    if (!json_find_string(object, "role", role, role_capacity))
        std::snprintf(role, role_capacity, "user");
    return json_find_string(object, "content", content, content_capacity);
}

void iso8601_now(char *output, std::size_t capacity)
{
    // Civil-from-days (Howard Hinnant); avoids importing gmtime().
    const long long seconds = static_cast<long long>(std::time(nullptr));
    long long days = seconds / 86400;
    const long long rest = seconds % 86400;
    days += 719468;
    const long long era = days / 146097;
    const long long doe = days - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long day = doy - (153 * mp + 2) / 5 + 1;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    std::snprintf(output, capacity, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ", year, month, day,
                  rest / 3600, rest / 60 % 60, rest % 60);
}

// --- generation ---------------------------------------------------------

bool run_generation(const gpt_runtime_message_t *messages, unsigned message_count,
                    char *response_text, std::size_t response_capacity, unsigned max_tokens = 512,
                    gpt_runtime_stats_t *reported_stats = nullptr)
{
    pthread_mutex_lock(&generation_mutex);
    const gpt_runtime_settings_t settings{0, max_tokens};
    gpt_runtime_stats_t stats{};
    const int result = gpt_runtime_generate(messages, message_count, settings, response_text,
                                            response_capacity, &stats, nullptr);
    pthread_mutex_unlock(&generation_mutex);
    if (reported_stats)
        *reported_stats = stats;
    return result == 0;
}

// --- HTTP plumbing --------------------------------------------------------

struct ConnectionArgs
{
    NetApi api;
    int sock;
    void *thread = nullptr;
    std::atomic<bool> done{false};
};

void send_all(const NetApi &api, int sock, const char *data, std::size_t length)
{
    std::size_t sent = 0;
    while (sent < length)
    {
        const int result =
            api.send(sock, data + sent, static_cast<unsigned long>(length - sent), 0);
        if (result <= 0)
            return;
        sent += static_cast<std::size_t>(result);
    }
}

void send_status(const NetApi &api, int sock, const char *status, const char *content_type,
                 const char *body)
{
    char header[256];
    const std::size_t body_length = std::strlen(body);
    std::snprintf(header, sizeof(header),
                  "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                  "Connection: close\r\n\r\n",
                  status, content_type, body_length);
    send_all(api, sock, header, std::strlen(header));
    send_all(api, sock, body, body_length);
}

void send_json(const NetApi &api, int sock, const char *json)
{
    send_status(api, sock, "200 OK", "application/json", json);
}

#ifdef PROSPERO_UI_VULKAN
#ifndef PROSPERO_WEB_ASSET_ROOT
#define PROSPERO_WEB_ASSET_ROOT "/app0/assets/web/"
#endif
// Public presentation assets only; model and generation routes retain bearer authentication.
bool serve_ui_asset(const NetApi &api, int sock, const char *path)
{
    struct Asset
    {
        const char *route;
        const char *file;
        const char *type;
    };
    static const Asset assets[] = {
        {"/", "index.html", "text/html; charset=utf-8"},
        {"/app.css", "app.css", "text/css; charset=utf-8"},
        {"/app.js", "app.js", "text/javascript; charset=utf-8"},
        {"/fonts/Inter-Regular.ttf", "fonts/Inter-Regular.ttf", "font/ttf"},
        {"/fonts/Inter-SemiBold.ttf", "fonts/Inter-SemiBold.ttf", "font/ttf"},
        {"/fonts/Montserrat-Medium.ttf", "fonts/Montserrat-Medium.ttf", "font/ttf"},
    };
    for (const auto &asset : assets)
    {
        if (std::strcmp(path, asset.route) != 0)
            continue;
        const std::string filename = std::string(PROSPERO_WEB_ASSET_ROOT) + asset.file;
        FILE *file = std::fopen(filename.c_str(), "rb");
        if (!file)
        {
            send_status(api, sock, "404 Not Found", "text/plain", "UI asset unavailable");
            return true;
        }
        std::fseek(file, 0, SEEK_END);
        const long size = std::ftell(file);
        std::rewind(file);
        if (size < 0 || size > 8 * 1024 * 1024)
        {
            std::fclose(file);
            send_status(api, sock, "500 Internal Server Error", "text/plain",
                        "UI asset unavailable");
            return true;
        }
        char header[512];
        std::snprintf(header, sizeof(header),
                      "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n"
                      "X-Content-Type-Options: nosniff\r\nCache-Control: no-cache\r\n"
                      "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src "
                      "'self'; connect-src 'self'; font-src 'self'; img-src 'self' data:; "
                      "frame-ancestors 'none'\r\nConnection: close\r\n\r\n",
                      asset.type, size);
        send_all(api, sock, header, std::strlen(header));
        char buffer[16384];
        for (std::size_t count; (count = std::fread(buffer, 1, sizeof(buffer), file)) != 0;)
            send_all(api, sock, buffer, count);
        std::fclose(file);
        return true;
    }
    return false;
}
#endif

const char kChatPage[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ProsperoAI</title><style>"
    "body{margin:0;background:#efe9dc;color:#28261f;"
    "font-family:'Montserrat',sans-serif;font-weight:500;"
    "display:flex;flex-direction:column;height:100vh}"
    "#top{padding:16px 20px 10px;font-size:20px;font-weight:700;color:#28261f;"
    "border-bottom:1px solid #d4c9b6}"
    "#log{flex:1;overflow-y:auto;padding:16px 20px}"
    ".msg{margin:0 0 14px;padding:14px 18px;max-width:85%;"
    "border-left:5px solid #c95b3f;background:#f7f2e8;"
    "white-space:pre-wrap;word-wrap:break-word}"
    ".role{display:block;font-size:12px;font-weight:700;letter-spacing:2px;"
    "color:#a84731;margin-bottom:6px;text-transform:uppercase}"
    ".user{margin-left:auto;border-left-color:#957f56;background:#e8dfcf}"
    ".user .role{color:#6d5a36}"
    "#bar{display:flex;padding:16px 20px;gap:10px;border-top:1px solid #d4c9b6}"
    "#text{flex:1;padding:14px 16px;border-radius:0;border:2px solid #b9ab94;"
    "background:#f7f2e8;color:#28261f;font-size:16px;font-family:inherit}"
    "#text:focus{outline:none;border-color:#c95b3f}"
    "#send{padding:14px 22px;border-radius:0;border:1px solid #d4c9b6;"
    "background:#f7f2e8;color:#a84731;font-size:16px;font-weight:700;"
    "font-family:inherit;letter-spacing:1px}"
    "</style></head><body>"
    "<div id=\"top\">ProsperoAI</div>"
    "<div id=\"log\"></div>"
    "<div id=\"bar\"><input id=\"text\" placeholder=\"Message ProsperoAI\">"
    "<button id=\"send\">Send</button></div>"
    "<script>"
    "const log=document.getElementById('log');"
    "const text=document.getElementById('text');"
    "const send=document.getElementById('send');"
    "const history=[];"
    "function bubble(role,content){"
    "const d=document.createElement('div');"
    "d.className='msg '+role;"
    "const label=document.createElement('span');"
    "label.className='role';label.textContent=role;"
    "d.appendChild(label);"
    "d.appendChild(document.createTextNode(content));"
    "log.appendChild(d);log.scrollTop=log.scrollHeight;return d;}"
    "async function submit(){"
    "const value=text.value.trim();if(!value)return;"
    "text.value='';history.push({role:'user',content:value});"
    "bubble('user',value);"
    "const reply=bubble('assistant','...');"
    "try{"
    "const response=await fetch('/api/chat',{method:'POST',"
    "body:JSON.stringify({messages:history})});"
    "const data=await response.json();"
    "const content=(data.message&&data.message.content)||data.error||'(no response)';"
    "reply.lastChild.textContent=content;"
    "history.push({role:'assistant',content:content});"
    "}catch(error){reply.lastChild.textContent='Error: '+error;}}"
    "send.addEventListener('click',submit);"
    "text.addEventListener('keydown',function(event){"
    "if(event.key==='Enter')submit();});"
    "</script></body></html>";

void handle_get_tags(const NetApi &api, int sock)
{
    char body[2048];
    std::snprintf(body, sizeof(body), "{\"models\":[");
    const unsigned count = gpt_runtime_model_count();
    for (unsigned i = 0; i < count; ++i)
    {
        char entry[256];
        std::snprintf(entry, sizeof(entry),
                      "%s{\"name\":\"%s\",\"model\":\"%s\",\"details\":{\"family\":\"%s\"}}",
                      i == 0 ? "" : ",", gpt_runtime_model_id(i), gpt_runtime_model_id(i),
                      gpt_runtime_model_purpose(i));
        std::strncat(body, entry, sizeof(body) - std::strlen(body) - 1);
    }
    std::strncat(body, "]}", sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

void handle_generate(const NetApi &api, int sock, const char *request_body)
{
    char model[128] = {};
    char prompt[3072] = {};
    json_find_string(request_body, "model", model, sizeof(model));
    if (!json_find_string(request_body, "prompt", prompt, sizeof(prompt)))
    {
        send_status(api, sock, "400 Bad Request", "application/json",
                    "{\"error\":\"missing prompt\"}");
        return;
    }
    const gpt_runtime_message_t messages[1] = {{"user", prompt}};
    char response_text[kResponseTextCapacity] = {};
    unsigned max_tokens = 512;
    const char *options = json_value_start(request_body, "options");
    const char *prediction = options ? json_value_start(options, "num_predict") : nullptr;
    if (prediction)
    {
        char *end = nullptr;
        const long value = std::strtol(prediction, &end, 10);
        if (end != prediction && value > 0 && value <= 512)
            max_tokens = static_cast<unsigned>(value);
    }
    gpt_runtime_stats_t stats{};
    const bool ok =
        run_generation(messages, 1, response_text, sizeof(response_text), max_tokens, &stats);

    char created_at[32];
    iso8601_now(created_at, sizeof(created_at));
    char body[kResponseCapacity];
    std::snprintf(body, sizeof(body), "{\"model\":\"%s\",\"created_at\":\"%s\",\"response\":\"",
                  model, created_at);
    json_append_escaped(body, sizeof(body), ok ? response_text : "");
    char timing[512];
    const auto decode_us = stats.elapsed_microseconds >= stats.prefill_microseconds
                               ? stats.elapsed_microseconds - stats.prefill_microseconds
                               : 0;
    std::snprintf(timing, sizeof(timing),
                  "\",\"done\":true,\"eval_count\":%u,\"prompt_eval_count\":%u,"
                  "\"eval_duration\":%llu,\"prompt_eval_duration\":%llu%s}",
                  stats.generated_tokens, stats.prompt_tokens,
                  static_cast<unsigned long long>(decode_us) * 1000,
                  static_cast<unsigned long long>(stats.prefill_microseconds) * 1000,
                  ok ? "" : ",\"error\":\"generation failed\"");
    std::strncat(body, timing, sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

void handle_chat(const NetApi &api, int sock, const char *request_body)
{
    char model[128] = {};
    json_find_string(request_body, "model", model, sizeof(model));
    const char *messages_array = json_value_start(request_body, "messages");
    gpt_runtime_message_t messages[16];
    char roles[16][32];
    char contents[16][2048];
    unsigned message_count = 0;
    if (messages_array != nullptr)
    {
        while (message_count < 16 &&
               json_find_message(messages_array, message_count, roles[message_count],
                                 sizeof(roles[message_count]), contents[message_count],
                                 sizeof(contents[message_count])))
        {
            messages[message_count].role = roles[message_count];
            messages[message_count].content = contents[message_count];
            ++message_count;
        }
    }
    if (message_count == 0)
    {
        send_status(api, sock, "400 Bad Request", "application/json",
                    "{\"error\":\"missing messages\"}");
        return;
    }

    char response_text[kResponseTextCapacity] = {};
    const bool ok = run_generation(messages, message_count, response_text, sizeof(response_text));

    char created_at[32];
    iso8601_now(created_at, sizeof(created_at));
    char body[kResponseCapacity];
    std::snprintf(body, sizeof(body),
                  "{\"model\":\"%s\",\"created_at\":\"%s\",\"message\":{\"role\":\"assistant\","
                  "\"content\":\"",
                  model, created_at);
    json_append_escaped(body, sizeof(body), ok ? response_text : "");
    std::strncat(body,
                 ok ? "\"},\"done\":true}" : "\"},\"done\":true,\"error\":\"generation failed\"}",
                 sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

#include "openai_api.inc"

#if defined(PS5_LLAMA_VULKAN) && defined(PROSPERO_UI_VULKAN)
void handle_model_download_status(const NetApi &api, int sock)
{
    prospero_model_download::poll();
    char status[192]{};
    prospero_model_download::status(status, sizeof(status));
    char body[4096] = "{\"state\":\"";
    const char *state = "idle";
    switch (prospero_model_download::state())
    {
    case prospero_model_download::State::Searching:
        state = "searching";
        break;
    case prospero_model_download::State::SearchReady:
        state = "search_ready";
        break;
    case prospero_model_download::State::Loading:
        state = "loading";
        break;
    case prospero_model_download::State::Ready:
        state = "ready";
        break;
    case prospero_model_download::State::Downloading:
        state = "downloading";
        break;
    case prospero_model_download::State::Complete:
        state = "complete";
        break;
    case prospero_model_download::State::Failed:
        state = "failed";
        break;
    default:
        break;
    }
    std::strncat(body, state, sizeof(body) - std::strlen(body) - 1);
    std::strncat(body, "\",\"status\":\"", sizeof(body) - std::strlen(body) - 1);
    json_append_escaped(body, sizeof(body), status);
    std::uint64_t completed = 0, total = 0;
    prospero_model_download::progress(&completed, &total);
    char progress[200];
    std::snprintf(progress, sizeof(progress),
                  "\",\"completed\":%llu,\"total\":%llu,\"active_preset\":%d,\"items\":[",
                  static_cast<unsigned long long>(completed),
                  static_cast<unsigned long long>(total), prospero_model_download::active_preset());
    std::strncat(body, progress, sizeof(body) - std::strlen(body) - 1);
    for (std::size_t i = 0; i < prospero_model_download::candidate_count(); ++i)
    {
        prospero_model_download::Candidate candidate{};
        if (!prospero_model_download::candidate(i, &candidate))
            continue;
        char item[512];
        std::snprintf(item, sizeof(item), "%s{\"name\":\"", i ? "," : "");
        std::strncat(body, item, sizeof(body) - std::strlen(body) - 1);
        json_append_escaped(body, sizeof(body), candidate.name);
        std::snprintf(item, sizeof(item),
                      prospero_model_download::state() ==
                              prospero_model_download::State::SearchReady
                          ? "\",\"downloads\":%llu}"
                          : "\",\"size\":%llu}",
                      static_cast<unsigned long long>(candidate.size));
        std::strncat(body, item, sizeof(body) - std::strlen(body) - 1);
    }
    std::strncat(body, "]}", sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

void handle_model_presets(const NetApi &api, int sock, const char *body = nullptr)
{
    if (body)
    {
        const char *value = json_value_start(body, "index");
        char *end = nullptr;
        const long index = value ? std::strtol(value, &end, 10) : -1;
        if (!value || end == value || index < 0 ||
            static_cast<std::size_t>(index) >= prospero_model_download::preset_count)
        {
            send_status(api, sock, "400 Bad Request", "application/json",
                        "{\"error\":\"a valid preset index is required\"}");
            return;
        }
        if (!prospero_model_download::download_preset(index))
        {
            send_status(api, sock, "409 Conflict", "application/json",
                        "{\"error\":\"Downloader is busy or this preset is already installed.\"}");
            return;
        }
    }
    nlohmann::json items = nlohmann::json::array();
    for (std::size_t i = 0; i < prospero_model_download::preset_count; ++i)
    {
        const auto &preset = prospero_model_download::presets[i];
        items.push_back({{"index", i},
                         {"id", preset.id},
                         {"name", preset.name},
                         {"kind", preset.kind},
                         {"source_filename", preset.kind == 0 ? preset.files[0].source : ""},
                         {"size", preset.size},
                         {"installed", prospero_model_download::preset_installed(i)}});
    }
    const auto response = nlohmann::json{{"data", items}}.dump();
    send_json(api, sock, response.c_str());
}

void handle_model_search_request(const NetApi &api, int sock, const char *body)
{
    char query[128]{};
    if (!json_find_string(body, "query", query, sizeof(query)))
    {
        send_status(api, sock, "400 Bad Request", "application/json",
                    "{\"error\":\"a model name is required\"}");
        return;
    }
    if (!prospero_model_download::search(query))
    {
        send_status(api, sock, "409 Conflict", "application/json",
                    "{\"error\":\"search is busy or the query is invalid\"}");
        return;
    }
    handle_model_download_status(api, sock);
}

void handle_model_download_action(const NetApi &api, int sock, const char *body,
                                  bool browse_repository)
{
    bool accepted = false;
    if (browse_repository)
    {
        char repository[160]{};
        if (!json_find_string(body, "repository", repository, sizeof(repository)))
        {
            send_status(api, sock, "400 Bad Request", "application/json",
                        "{\"error\":\"repository is required as owner/name\"}");
            return;
        }
        accepted = prospero_model_download::browse(repository);
    }
    else
    {
        const char *value = json_value_start(body, "index");
        char *end = nullptr;
        const long index = value ? std::strtol(value, &end, 10) : -1;
        if (!value || end == value || index < 0 || index > 7)
        {
            send_status(api, sock, "400 Bad Request", "application/json",
                        "{\"error\":\"a valid GGUF item index is required\"}");
            return;
        }
        accepted = prospero_model_download::download(static_cast<std::size_t>(index));
    }
    if (!accepted)
    {
        send_status(api, sock, "409 Conflict", "application/json",
                    "{\"error\":\"downloader is busy or the request is invalid\"}");
        return;
    }
    handle_model_download_status(api, sock);
}
#endif

std::size_t read_request(const NetApi &api, int sock, char *buffer, std::size_t capacity)
{
    std::size_t total = 0;
    const char *header_end = nullptr;
    while (total + 1 < capacity)
    {
        const int received = api.recv(sock, buffer + total, capacity - 1 - total, 0);
        if (received <= 0)
            return 0;
        total += static_cast<std::size_t>(received);
        buffer[total] = '\0';
        header_end = std::strstr(buffer, "\r\n\r\n");
        if (header_end)
            break;
        if (total > 16384)
            break;
    }
    if (!header_end)
    {
        openai_error(api, sock, "431 Request Header Fields Too Large", "invalid_headers",
                     "Incomplete or oversized headers.");
        return 0;
    }
    std::size_t content_length = 0;
    bool has_length = false;
    const char *line = std::strstr(buffer, "\r\n");
    while (line && line < header_end)
    {
        line += 2;
        const char *end = std::strstr(line, "\r\n");
        if (!end)
            return 0;
        std::string header(line, end);
        auto colon = header.find(':');
        if (colon != std::string::npos)
        {
            std::string key = header.substr(0, colon), value = header.substr(colon + 1);
            for (char &c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (key == "transfer-encoding")
            {
                openai_error(api, sock, "400 Bad Request", "unsupported_transfer_encoding",
                             "Use Content-Length; chunked requests are not supported.");
                return 0;
            }
            if (key == "content-length")
            {
                auto start = value.find_first_not_of(" \t");
                auto finish = value.find_last_not_of(" \t");
                if (start == std::string::npos || has_length)
                    return 0;
                value = value.substr(start, finish - start + 1);
                content_length = 0;
                for (char c : value)
                {
                    if (c < '0' || c > '9')
                    {
                        openai_error(api, sock, "400 Bad Request", "invalid_content_length",
                                     "Invalid Content-Length.");
                        return 0;
                    }
                    if (content_length > capacity / 10)
                    {
                        openai_error(api, sock, "413 Payload Too Large", "request_too_large",
                                     "Request limit is 256 KiB including headers.");
                        return 0;
                    }
                    content_length = content_length * 10 + (c - '0');
                }
                has_length = true;
            }
        }
        line = end;
    }
    const std::size_t body_start = header_end - buffer + 4;
    if (content_length >= capacity - body_start)
    {
        openai_error(api, sock, "413 Payload Too Large", "request_too_large",
                     "Request limit is 256 KiB including headers.");
        return 0;
    }
    const std::size_t wanted = body_start + content_length;
    while (total < wanted)
    {
        const int received = api.recv(sock, buffer + total, wanted - total, 0);
        if (received <= 0)
            return 0;
        total += received;
    }
    buffer[wanted] = '\0';
    return wanted;
}

void handle_connection(const NetApi &api, int sock)
{
    auto storage = std::make_unique<char[]>(kRequestCapacity);
    char *request = storage.get();
    const std::size_t length = read_request(api, sock, request, kRequestCapacity);
    if (length == 0)
    {
        api.socket_close(sock);
        return;
    }

    char method[8] = {};
    char path[256] = {};
    std::sscanf(request, "%7s %255s", method, path);
    const char *header_end = std::strstr(request, "\r\n\r\n");
    const char *body = header_end != nullptr ? header_end + 4 : "";

#ifdef PROSPERO_UI_VULKAN
    if (std::strcmp(method, "GET") == 0 && serve_ui_asset(api, sock, path))
    {
        api.socket_close(sock);
        return;
    }
#endif
    if (!api_authorized(request))
        openai_error(api, sock, "401 Unauthorized", "invalid_api_key",
                     "A valid bearer API key is required.");
#ifdef PS5_LLAMA_VULKAN
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/v1/models") == 0)
        handle_openai_models(api, sock);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/v1/chat/completions") == 0)
        handle_openai_chat(api, sock, body);
#endif
#if defined(PS5_LLAMA_VULKAN) && defined(PROSPERO_UI_VULKAN)
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/models/presets") == 0)
        handle_model_presets(api, sock);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/presets") == 0)
        handle_model_presets(api, sock, body);
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/models/download") == 0)
        handle_model_download_status(api, sock);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/search") == 0)
        handle_model_search_request(api, sock, body);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/browse") == 0)
        handle_model_download_action(api, sock, body, true);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/download") == 0)
        handle_model_download_action(api, sock, body, false);
#endif
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/") == 0)
        send_status(api, sock, "200 OK", "text/html; charset=utf-8", kChatPage);
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/tags") == 0)
        handle_get_tags(api, sock);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/generate") == 0)
        handle_generate(api, sock, body);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/chat") == 0)
        handle_chat(api, sock, body);
    else
        send_status(api, sock, "404 Not Found", "application/json", "{\"error\":\"not found\"}");

    api.socket_close(sock);
}

void *connection_worker(void *argument)
{
    auto *args = static_cast<ConnectionArgs *>(argument);
    handle_connection(args->api, args->sock);
    args->done.store(true, std::memory_order_release);
    return nullptr;
}

void *server_worker(void *)
{
    NetApi api{};
    if (!bring_up_net_api(&api))
    {
        char line[128];
        std::snprintf(line, sizeof(line), "HTTP off (%s)", last_net_detail);
        log_line(line);
        return nullptr;
    }
    char up[128];
    std::snprintf(up, sizeof(up), "HTTP net up (%s)", last_net_detail);
    log_line(up);

    const int listen_sock = api.socket("prosperoai-http", kAfInet, kSockStream, 0);
    log_result("sceNetSocket(listen)", listen_sock);
    if (listen_sock < 0)
        return nullptr;

    const int reuse = 1;
    api.setsockopt(listen_sock, kSolSocket, kSoReuseAddr, &reuse, sizeof(reuse));

    SceNetSockaddrIn address{};
    address.sin_len = sizeof(address);
    address.sin_family = static_cast<std::uint8_t>(kAfInet);
    address.sin_port = static_cast<std::uint16_t>((kHttpServerPort >> 8) | (kHttpServerPort << 8));
    address.sin_addr = 0; // INADDR_ANY

    log_result("sceNetBind", api.bind(listen_sock, &address, sizeof(address)));
    log_result("sceNetListen", api.listen(listen_sock, kListenBacklog));
    log_line("HTTP server listening");

    std::vector<ConnectionArgs *> connections;
    for (;;)
    {
        SceNetSockaddrIn client{};
        unsigned int client_length = sizeof(client);
        const int client_sock = api.accept(listen_sock, &client, &client_length);
        if (client_sock < 0)
            continue;

        for (auto it = connections.begin(); it != connections.end();)
        {
            if ((*it)->done.load(std::memory_order_acquire))
            {
                scePthreadJoin((*it)->thread, nullptr);
                delete *it;
                it = connections.erase(it);
            }
            else
                ++it;
        }
        if (connections.size() >= 4)
        {
            openai_error(api, client_sock, "503 Service Unavailable", "server_busy",
                         "Too many active connections.");
            api.socket_close(client_sock);
            continue;
        }
        auto *args = new (std::nothrow) ConnectionArgs{api, client_sock};
        if (args == nullptr)
        {
            api.socket_close(client_sock);
            continue;
        }

        pthread_attr_t attributes;
        int result = pthread_attr_init(&attributes);
        const bool attributes_initialized = result == 0;
        if (result == 0)
            result = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
        void *thread = nullptr;
        if (result == 0)
            result = scePthreadCreate(&thread, &attributes, connection_worker, args,
                                      "prosperoai-http-conn");
        if (attributes_initialized)
            pthread_attr_destroy(&attributes);
        if (result == 0)
        {
            // Join finished workers on the next accept; avoid the broken detach path.
            args->thread = thread;
            connections.push_back(args);
        }
        else
        {
            api.socket_close(client_sock);
            delete args;
        }
    }
}

} // namespace

void prospero_http_server_last_status(char *text, std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(last_status_mutex);
    std::snprintf(text, capacity, "%s", last_status);
}

bool prospero_http_server_start(unsigned short port)
{
    kHttpServerPort = port;
    load_api_key();
    void *thread = nullptr;
    const int created =
        scePthreadCreate(&thread, nullptr, server_worker, nullptr, "prosperoai-http-server");
    if (created != 0)
        return false;
    (void)thread;
    return true;
}

asm(".section .text.prosperoai_layout_probe,\"axR\",@progbits\n"
    ".globl prosperoai_layout_probe\n"
    "prosperoai_layout_probe:\n"
    ".fill 0x3000,1,0xcc\n"
    ".text\n");
