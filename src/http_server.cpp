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

#include "gpt_app.hpp"
#include "gpt_runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>

extern "C"
{
    int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                         void *argument, const char *name);
    int sceNetInit(void);
    int sceNetPoolCreate(const char *name, int size, int flags);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetSocketClose(int sock);
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

void log_line(const char *text)
{
    if (std::strncmp(text, "NET:", 4) == 0)
        std::snprintf(last_net_detail, sizeof(last_net_detail), "%s", text);
    ProsperoAiApp::SetExternalStatus(text);
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
constexpr std::size_t kRequestCapacity = 16384;
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
    api->bind = sceNetBind;
    api->listen = sceNetListen;
    api->accept = sceNetAccept;
    api->send = sceNetSend;
    api->recv = sceNetRecv;
    api->setsockopt = sceNetSetsockopt;

    const int init = api->init();
    log_result("sceNetInit", init);
    if (init < 0)
        return false;
    const int pool = api->pool_create("prosperoai-http", kNetPoolSize, 0);
    log_result("sceNetPoolCreate", pool);
    return pool >= 0;
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
                    char *response_text, std::size_t response_capacity)
{
    pthread_mutex_lock(&generation_mutex);
    const gpt_runtime_settings_t settings{0, 512};
    gpt_runtime_stats_t stats{};
    const int result = gpt_runtime_generate(messages, message_count, settings, response_text,
                                            response_capacity, &stats, nullptr);
    pthread_mutex_unlock(&generation_mutex);
    return result == 0;
}

// --- HTTP plumbing --------------------------------------------------------

struct ConnectionArgs
{
    NetApi api;
    int sock;
};

void send_all(const NetApi &api, int sock, const char *data, std::size_t length)
{
    std::size_t sent = 0;
    while (sent < length)
    {
        const int result = api.send(sock, data + sent, static_cast<unsigned long>(length - sent), 0);
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

const char kChatPage[] = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ProsperoAI</title>
<style>
:root{--bg:#ebe5d8;--panel:#f7f3ea;--ink:#2a2420;--muted:#7a6f63;--accent:#c0462a;--line:#d8cfbf}
body{margin:0;background:var(--bg);color:var(--ink);font-family:sans-serif;display:flex;flex-direction:column;height:100vh}
header{display:flex;align-items:center;gap:8px;padding:10px 12px;background:var(--panel);border-bottom:1px solid var(--line)}
header b{color:var(--accent);margin-right:12px}
.tab{padding:8px 14px;border:none;background:none;color:var(--muted);font-size:15px;border-bottom:3px solid transparent}
.tab.active{color:var(--ink);border-bottom-color:var(--accent)}
main{flex:1;overflow:hidden;position:relative}
.screen{position:absolute;inset:0;overflow-y:auto;padding:12px;display:none}
.screen.active{display:block}
#log{display:flex;flex-direction:column;gap:8px}
.msg{padding:8px 12px;border-radius:10px;max-width:85%;white-space:pre-wrap;word-wrap:break-word}
.user{background:var(--accent);color:#fff;align-self:flex-end}
.assistant{background:var(--panel);border:1px solid var(--line)}
#bar{display:flex;gap:8px;padding:8px;background:var(--panel);border-top:1px solid var(--line)}
#text{flex:1;padding:10px;border-radius:8px;border:1px solid var(--line);font-size:16px}
button.primary{padding:10px 16px;border-radius:8px;border:none;background:var(--accent);color:#fff;font-size:15px}
button.ghost{padding:8px 12px;border-radius:8px;border:1px solid var(--line);background:var(--panel);font-size:14px}
.row{display:flex;align-items:center;gap:10px;padding:12px;margin:8px 0;background:var(--panel);border:1px solid var(--line);border-radius:10px}
.row.focused{border-color:var(--accent)}
.row .name{flex:1;font-weight:600}
.row .meta{color:var(--muted);font-size:13px}
.badge{background:var(--accent);color:#fff;border-radius:6px;padding:2px 6px;font-size:12px}
.field{margin:10px 0}
.field label{display:block;color:var(--muted);font-size:13px;margin-bottom:4px}
.field select,.field input{padding:8px;border-radius:8px;border:1px solid var(--line);font-size:15px;width:100%;box-sizing:border-box}
.status{color:var(--muted);font-size:13px;padding:6px 12px}
</style></head><body>
<header><b>ProsperoAI</b>
<button class="tab active" data-tab="conv">Conversation</button>
<button class="tab" data-tab="models">Models</button>
<button class="tab" data-tab="settings">Settings</button></header>
<main>
<section id="conv" class="screen active"><div id="log"></div></section>
<section id="models" class="screen"><div id="model-list"></div><div id="models-status" class="status"></div></section>
<section id="settings" class="screen">
<div class="field"><label for="active">Active model</label><select id="active"></select></div>
<div class="field"><label for="tokens">Output limit (tokens)</label><input id="tokens" type="number" min="16" max="2048" value="64"></div>
<div class="field"><label for="style">Response style</label><select id="style"><option value="0">Balanced</option><option value="1">Concise</option><option value="2">Detailed</option></select></div>
<p class="meta">Output limit and style are kept in this browser. The active model is stored on the PS5.</p>
</section>
</main>
<div id="bar"><input id="text" placeholder="Message ProsperoAI"><button id="send" class="primary">Send</button></div>
<script>
const $=id=>document.getElementById(id);
const log=$('log'),text=$('text'),send=$('send'),history=[];
const saved=JSON.parse(localStorage.getItem('prospero-settings')||'{}');
if(saved.tokens)$('tokens').value=saved.tokens;
if(saved.style!==undefined)$('style').value=saved.style;
function saveSettings(){localStorage.setItem('prospero-settings',JSON.stringify({tokens:$('tokens').value,style:$('style').value}));}
$('tokens').addEventListener('change',saveSettings);$('style').addEventListener('change',saveSettings);
function show(tab){
  document.querySelectorAll('.tab').forEach(b=>b.classList.toggle('active',b.dataset.tab===tab));
  document.querySelectorAll('.screen').forEach(s=>s.classList.toggle('active',s.id===tab));
  if(tab==='models'||tab==='settings')loadModels();
}
document.querySelectorAll('.tab').forEach(b=>b.addEventListener('click',()=>show(b.dataset.tab)));
function bubble(role,content){const d=document.createElement('div');d.className='msg '+role;d.textContent=content;log.appendChild(d);log.scrollTop=log.scrollHeight;return d;}
async function submit(){
  const value=text.value.trim();if(!value)return;
  text.value='';history.push({role:'user',content:value});bubble('user',value);
  const reply=bubble('assistant','...');
  try{
    const response=await fetch('/api/chat',{method:'POST',body:JSON.stringify({messages:history})});
    const data=await response.json();
    const content=(data.message&&data.message.content)||data.error||'(no response)';
    reply.textContent=content;history.push({role:'assistant',content:content});
  }catch(error){reply.textContent='Error: '+error;}
}
send.addEventListener('click',submit);
text.addEventListener('keydown',e=>{if(e.key==='Enter')submit();});
async function loadModels(){
  const response=await fetch('/api/models');const data=await response.json();
  const list=$('model-list'),active=$('active');
  list.innerHTML='';active.innerHTML='';
  if(!data.models.length){list.textContent='No models installed. Copy model folders to PPSA99004/models/.';}
  data.models.forEach(m=>{
    const row=document.createElement('div');row.className='row'+(m.active?' focused':'');
    row.innerHTML='<div class="name"></div>';
    row.querySelector('.name').textContent=m.id;
    const meta=document.createElement('div');meta.className='meta';meta.textContent=m.purpose+(m.active?' · active':'');
    row.firstChild.appendChild(meta);
    if(!m.active){const del=document.createElement('button');del.className='ghost';del.textContent='Delete';
      del.addEventListener('click',()=>removeModel(m.index,m.id));row.appendChild(del);}
    else{const b=document.createElement('span');b.className='badge';b.textContent='ACTIVE';row.appendChild(b);}
    list.appendChild(row);
    const opt=document.createElement('option');opt.value=m.index;opt.textContent=m.id;if(m.active)opt.selected=true;active.appendChild(opt);
  });
}
async function removeModel(index,id){
  if(!confirm('Delete '+id+'?'))return;
  const response=await fetch('/api/models/delete',{method:'POST',body:JSON.stringify({index:index})});
  const data=await response.json();
  $('models-status').textContent=data.ok?'Deleted '+id:(data.error||'Delete failed');
  loadModels();
}
$('active').addEventListener('change',async e=>{
  const response=await fetch('/api/models/select',{method:'POST',body:JSON.stringify({index:Number(e.target.value)})});
  const data=await response.json();$('models-status').textContent=data.ok?'Active model changed':(data.error||'Select failed');
  loadModels();
});
</script></body></html>)HTML";

bool json_find_index(const char *body, unsigned *out)
{
    const char *at = json_value_start(body, "index");
    if (at == nullptr || *at < '0' || *at > '9')
        return false;
    *out = static_cast<unsigned>(std::strtoul(at, nullptr, 10));
    return true;
}

void handle_get_models(const NetApi &api, int sock)
{
    char body[4096];
    const unsigned selected = gpt_runtime_selected_model();
    std::snprintf(body, sizeof(body), "{\"selected\":%u,\"models\":[", selected);
    const unsigned count = gpt_runtime_model_count();
    for (unsigned i = 0; i < count; ++i)
    {
        char entry[256];
        std::snprintf(entry, sizeof(entry),
                      "%s{\"index\":%u,\"id\":\"%s\",\"purpose\":\"%s\",\"active\":%s}",
                      i == 0 ? "" : ",", i, gpt_runtime_model_id(i), gpt_runtime_model_purpose(i),
                      i == selected ? "true" : "false");
        std::strncat(body, entry, sizeof(body) - std::strlen(body) - 1);
    }
    std::strncat(body, "]}", sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

void handle_delete_model(const NetApi &api, int sock, const char *request_body)
{
    unsigned index = 0;
    if (!json_find_index(request_body, &index))
    {
        send_status(api, sock, "400 Bad Request", "application/json", "{\"ok\":false,\"error\":\"missing index\"}");
        return;
    }
    if (!gpt_runtime_delete_model(index))
    {
        send_status(api, sock, "409 Conflict", "application/json",
                    "{\"ok\":false,\"error\":\"cannot delete the active or an unknown model\"}");
        return;
    }
    send_json(api, sock, "{\"ok\":true}");
}

void handle_select_model(const NetApi &api, int sock, const char *request_body)
{
    unsigned index = 0;
    if (!json_find_index(request_body, &index) || !gpt_runtime_select_model(index))
    {
        send_status(api, sock, "400 Bad Request", "application/json",
                    "{\"ok\":false,\"error\":\"unknown model\"}");
        return;
    }
    send_json(api, sock, "{\"ok\":true}");
}

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
        send_status(api, sock, "400 Bad Request", "application/json", "{\"error\":\"missing prompt\"}");
        return;
    }
    const gpt_runtime_message_t messages[1] = {{"user", prompt}};
    char response_text[kResponseTextCapacity] = {};
    const bool ok = run_generation(messages, 1, response_text, sizeof(response_text));

    char created_at[32];
    iso8601_now(created_at, sizeof(created_at));
    char body[kResponseCapacity];
    std::snprintf(body, sizeof(body), "{\"model\":\"%s\",\"created_at\":\"%s\",\"response\":\"",
                 model, created_at);
    json_append_escaped(body, sizeof(body), ok ? response_text : "");
    std::strncat(body, ok ? "\",\"done\":true}" : "\",\"done\":true,\"error\":\"generation failed\"}",
                sizeof(body) - std::strlen(body) - 1);
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
        send_status(api, sock, "400 Bad Request", "application/json", "{\"error\":\"missing messages\"}");
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
    std::strncat(body, ok ? "\"},\"done\":true}" : "\"},\"done\":true,\"error\":\"generation failed\"}",
                sizeof(body) - std::strlen(body) - 1);
    send_json(api, sock, body);
}

std::size_t read_request(const NetApi &api, int sock, char *buffer, std::size_t capacity)
{
    std::size_t total = 0;
    const char *header_end = nullptr;
    while (total + 1 < capacity)
    {
        const int received =
            api.recv(sock, buffer + total, static_cast<unsigned long>(capacity - 1 - total), 0);
        if (received <= 0)
            return total;
        total += static_cast<std::size_t>(received);
        buffer[total] = '\0';
        header_end = std::strstr(buffer, "\r\n\r\n");
        if (header_end != nullptr)
            break;
    }
    if (header_end == nullptr)
        return total;

    std::size_t content_length = 0;
    const char *length_header = std::strstr(buffer, "Content-Length:");
    if (length_header != nullptr)
        content_length = static_cast<std::size_t>(std::atoi(length_header + 15));

    const std::size_t body_start = static_cast<std::size_t>(header_end - buffer) + 4;
    const std::size_t wanted = body_start + content_length;
    while (total < wanted && total + 1 < capacity)
    {
        const int received =
            api.recv(sock, buffer + total, static_cast<unsigned long>(capacity - 1 - total), 0);
        if (received <= 0)
            break;
        total += static_cast<std::size_t>(received);
        buffer[total] = '\0';
    }
    return total;
}

void handle_connection(const NetApi &api, int sock)
{
    char request[kRequestCapacity];
    const std::size_t length = read_request(api, sock, request, sizeof(request));
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

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/") == 0)
        send_status(api, sock, "200 OK", "text/html; charset=utf-8", kChatPage);
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/tags") == 0)
        handle_get_tags(api, sock);
    else if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/models") == 0)
        handle_get_models(api, sock);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/delete") == 0)
        handle_delete_model(api, sock, body);
    else if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/models/select") == 0)
        handle_select_model(api, sock, body);
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
    delete args;
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

    for (;;)
    {
        SceNetSockaddrIn client{};
        unsigned int client_length = sizeof(client);
        const int client_sock = api.accept(listen_sock, &client, &client_length);
        if (client_sock < 0)
            continue;

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
            result = pthread_attr_setstacksize(&attributes, 256 * 1024);
        void *thread = nullptr;
        if (result == 0)
            result = scePthreadCreate(&thread, &attributes, connection_worker, args,
                                      "prosperoai-http-conn");
        if (attributes_initialized)
            pthread_attr_destroy(&attributes);
        if (result == 0)
        {
            // Deliberately not detached: scePthreadDetach() on this
            // runtime shim crashes the title outright (bisected on real
            // hardware). Each connection thread's handle is leaked instead
            // of joined/detached, which is an acceptable tradeoff for a
            // low-traffic embedded server.
            (void)thread;
        }
        else
        {
            api.socket_close(client_sock);
            delete args;
        }
    }
}

} // namespace

bool prospero_http_server_start(unsigned short port)
{
    kHttpServerPort = port;
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
