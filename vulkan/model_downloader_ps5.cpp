// ProsperoAI — Hugging Face GGUF downloader for the PS5 Vulkan build.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef PS5_LLAMA_VULKAN
#include "model_downloader_ps5.hpp"
#include "gpt_runtime.hpp"
#include "model_paths.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <nlohmann/json.hpp>

extern "C" {
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(std::size_t);
int sceSslTerm(int);
int sceHttp2Init(int, int, std::size_t, int);
int sceHttp2Term(int);
int sceHttp2CreateTemplate(int, const char *, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2CreateRequestWithURL(int, const char *, const char *, std::uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void *, std::size_t);
int sceHttp2GetStatusCode(int, int *);
int sceHttp2SetAutoRedirect(int, int);
int sceHttp2GetAllResponseHeaders(int, char **, std::size_t *);
int sceHttp2GetResponseContentLength(int, std::uint64_t *);
int sceHttp2ReadData(int, void *, std::size_t);
int scePthreadCreate(void **, const void *, void *(*)(void *), void *, const char *);
int scePthreadJoin(void *, void **);
}

namespace prospero_model_download {
namespace {
using Json = nlohmann::json;
constexpr const char *kModelRoot = prospero::kModelRoot;
constexpr std::uint64_t kMaxCatalogBytes = 8 * 1024 * 1024;
// Keep enough headroom for Vulkan allocations and KV cache on the console.
constexpr std::uint64_t kMaxModelBytes = 7ULL * 1024 * 1024 * 1024;
struct Item { std::string name, sha256; std::uint64_t size = 0; };
std::mutex state_mutex;
std::mutex job_mutex;
std::vector<Item> items;
std::atomic<State> current{State::Idle};
std::atomic<std::uint64_t> completed_bytes{0}, total_bytes{0};
char current_status[160] = "Enter a Hugging Face GGUF repository.";
void *worker_thread = nullptr;
std::atomic<bool> worker_done{false};
std::string worker_query;
std::string worker_repo;
std::size_t worker_index;
std::atomic<int> worker_preset{-1};

void set_status(const char *text) {
    std::lock_guard<std::mutex> lock(state_mutex);
    std::snprintf(current_status, sizeof(current_status), "%s", text ? text : "");
}
void set_error(const char *stage, int code) {
    char text[160];
    std::snprintf(text, sizeof(text), "%s failed (0x%08X). Check network access and HTTPS permissions.",
                  stage, static_cast<unsigned>(code));
    set_status(text);
    current.store(State::Failed, std::memory_order_release);
}

bool safe_repo(const std::string &repo) {
    if (repo.size() < 3 || repo.size() > 128 || std::count(repo.begin(), repo.end(), '/') != 1) return false;
    bool segment_start = true;
    for (char c : repo) {
        if (c == '/') { if (segment_start) return false; segment_start = true; continue; }
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
        if (segment_start && c == '.') return false;
        segment_start = false;
    }
    return !segment_start;
}
bool safe_filename(const std::string &name) {
    if (name.empty() || name.size() > 256 || name.front() == '/' || name.find("..") != std::string::npos) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '/')) return false;
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".gguf") == 0 &&
           name.find("mmproj") == std::string::npos &&
           name.find("-of-") == std::string::npos;
}
std::string encode_path(const std::string &path) {
    static const char hex[] = "0123456789ABCDEF";
    std::string output;
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == '~') output += c;
        else { output += '%'; output += hex[c >> 4]; output += hex[c & 15]; }
    }
    return output;
}

struct HttpSession {
    int pool = -1, ssl = -1, http = -1, tmpl = -1, request = -1;
    bool open() {
        // Networking is initialized by the app's HTTP server during startup.
        pool = sceNetPoolCreate("prosperoai-model-download", 256 * 1024, 0);
        if (pool < 0) { set_error("Network pool", pool); return false; }
        ssl = sceSslInit(512 * 1024);
        if (ssl < 0) { set_error("HTTPS/TLS", ssl); return false; }
        http = sceHttp2Init(pool, ssl, 1024 * 1024, 1);
        if (http < 0) { set_error("HTTPS client", http); return false; }
        tmpl = sceHttp2CreateTemplate(http, "ProsperoAI/1.0", 3, 1);
        if (tmpl < 0) { set_error("HTTPS template", tmpl); return false; }
        return true;
    }
    void close_request() { if (request >= 0) { sceHttp2DeleteRequest(request); request = -1; } }
    void close() {
        close_request();
        if (tmpl >= 0) sceHttp2DeleteTemplate(tmpl);
        if (http >= 0) sceHttp2Term(http);
        if (ssl >= 0) sceSslTerm(ssl);
        if (pool >= 0) sceNetPoolDestroy(pool);
    }
    ~HttpSession() { close(); }
    bool get(const std::string &url) {
        std::string address = url;
        // Follow redirects explicitly. The firmware's automatic redirect path can
        // report HTTP 200 while returning an empty body for Hugging Face CDN files.
        for (int hop = 0; hop < 8; ++hop) {
            close_request();
            request = sceHttp2CreateRequestWithURL(tmpl, "GET", address.c_str(), 0);
            if (request < 0) { set_error("HTTPS request", request); return false; }
            int result = sceHttp2SetAutoRedirect(request, 0);
            if (result < 0) { set_error("HTTPS redirect setup", result); return false; }
            result = sceHttp2SendRequest(request, nullptr, 0);
            if (result < 0) { set_error("HTTPS send", result); close_request(); return false; }
            int status = 0;
            result = sceHttp2GetStatusCode(request, &status);
            if (result < 0) { set_error("HTTPS response", result); return false; }
            if (status == 200) return true;
            if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) {
                set_error("Hugging Face response", status); return false;
            }
            char *headers = nullptr;
            std::size_t length = 0;
            result = sceHttp2GetAllResponseHeaders(request, &headers, &length);
            if (result < 0 || !headers || length > 65536) { set_error("HTTPS redirect headers", result); return false; }
            std::string redirect;
            const std::string text(headers, length);
            for (std::size_t start = 0; start < text.size();) {
                const auto end = text.find('\n', start);
                std::string line = text.substr(start, end == std::string::npos ? end : end - start);
                std::string key = line.substr(0, 9);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                if (key == "location:") {
                    redirect = line.substr(9);
                    const auto first = redirect.find_first_not_of(" \t");
                    if (first != std::string::npos) redirect.erase(0, first);
                    const auto last = redirect.find_last_not_of(std::string("\r\n \t\0", 5));
                    if (last != std::string::npos) redirect.resize(last + 1);
                    break;
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            if (!redirect.empty() && redirect.front() == '/') {
                const auto host_end = address.find('/', 8);
                redirect = address.substr(0, host_end) + redirect;
            }
            if (redirect.compare(0, 8, "https://") != 0) {
                set_status("Model host returned an invalid HTTPS redirect."); current.store(State::Failed); return false;
            }
            address = std::move(redirect);
        }
        set_status("Model host returned too many redirects."); current.store(State::Failed); return false;
    }
};

bool read_catalog(const std::string &repo, std::vector<Item> *output) {
    HttpSession http;
    if (!http.open() || !http.get("https://huggingface.co/api/models/" + repo +
                                  "/tree/main?recursive=true&expand=true")) return false;
    std::string body;
    std::array<char, 32768> chunk{};
    for (;;) {
        int n = sceHttp2ReadData(http.request, chunk.data(), chunk.size());
        if (n < 0) { set_error("Catalog download", n); return false; }
        if (n == 0) break;
        if (body.size() + static_cast<std::size_t>(n) > kMaxCatalogBytes) {
            set_status("Repository catalog is too large (8 MiB limit)."); current.store(State::Failed); return false;
        }
        body.append(chunk.data(), n);
    }
    const Json entries = Json::parse(body, nullptr, false);
    if (entries.is_discarded() || !entries.is_array()) {
        set_status("Hugging Face returned an invalid model catalog."); current.store(State::Failed); return false;
    }
    for (const auto &entry : entries) {
        if (!entry.is_object() || !entry.contains("path") || !entry["path"].is_string()) continue;
        Item item; item.name = entry["path"].get<std::string>();
        if (!safe_filename(item.name)) continue;
        if (entry.contains("size") && entry["size"].is_number_integer()) {
            const auto value = entry["size"].get<std::int64_t>();
            if (value > 0) item.size = static_cast<std::uint64_t>(value);
        }
        if (entry.contains("lfs") && entry["lfs"].is_object()) {
            const auto &lfs = entry["lfs"];
            if (lfs.contains("size") && lfs["size"].is_number_integer()) {
                const auto value = lfs["size"].get<std::int64_t>();
                if (value > 0) item.size = static_cast<std::uint64_t>(value);
            }
            if (lfs.contains("oid") && lfs["oid"].is_string()) item.sha256 = lfs["oid"].get<std::string>();
        }
        if (item.sha256.compare(0, 7, "sha256:") == 0) item.sha256.erase(0, 7);
        if (item.size == 0 || item.size > kMaxModelBytes || item.sha256.size() != 64) continue;
        output->push_back(std::move(item));
    }
    std::sort(output->begin(), output->end(), [](const Item &a, const Item &b) {
        const auto rank = [](const std::string &name) {
            if (name.find("Q4_K_M") != std::string::npos) return 0;
            if (name.find("Q4_0") != std::string::npos) return 1;
            if (name.find("Q5_K_M") != std::string::npos) return 2;
            if (name.find("Q3_K_M") != std::string::npos) return 3;
            if (name.find("Q2_K") != std::string::npos) return 4;
            return 5;
        };
        int ar = rank(a.name), br = rank(b.name);
        return ar != br ? ar < br : a.size < b.size;
    });
    // The screen has eight rows; show the strongest broadly useful quantizations.
    if (output->size() > 8) output->resize(8);
    return true;
}

std::string installed_name(const std::string &repo, const std::string &filename) {
    std::string repo_name = repo;
    std::replace(repo_name.begin(), repo_name.end(), '/', '-');
    std::string leaf = filename.substr(filename.find_last_of('/') == std::string::npos ? 0 : filename.find_last_of('/') + 1);
    return repo_name + "--" + leaf;
}
bool ensure_model_root() {
    // The shared leaf may be mounted even though sandboxed parent directories
    // reject mkdir/access. Check the mounted directory itself first.
    struct stat directory{};
    if (stat(kModelRoot, &directory) == 0 && S_ISDIR(directory.st_mode)) return true;
    if (mkdir("/data/homebrew/prosperoai", 0777) != 0 && errno != EEXIST) return false;
    return mkdir(kModelRoot, 0777) == 0 ||
           (stat(kModelRoot, &directory) == 0 && S_ISDIR(directory.st_mode));
}
void storage_error(const char *operation) {
    const int error = errno;
    char message[160];
    std::snprintf(message, sizeof(message), "%s: %s (%d).%s", operation,
                  std::strerror(error), error,
                  error == EACCES || error == ENOENT
                      ? " Shared model storage must be connected after console restart."
                      : " Check available storage.");
    set_status(message);
    current.store(State::Failed);
}

// Streaming SHA-256 so multi-gigabyte files never need to be loaded into RAM.
class Sha256 {
    std::array<std::uint32_t, 8> h{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t, 64> block{};
    std::uint64_t length = 0;
    std::size_t used = 0;
    static std::uint32_t r(std::uint32_t x, int n) { return (x >> n) | (x << (32-n)); }
    void transform(const std::uint8_t *p) {
        static constexpr std::uint32_t k[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for(int i=0;i<16;i++) w[i]=(std::uint32_t(p[i*4])<<24)|(std::uint32_t(p[i*4+1])<<16)|(std::uint32_t(p[i*4+2])<<8)|p[i*4+3];
        for(int i=16;i<64;i++){auto a=r(w[i-15],7)^r(w[i-15],18)^(w[i-15]>>3);auto b=r(w[i-2],17)^r(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+a+w[i-7]+b;}
        auto [a,b,c,d,e,f,g,x]=h;
        for(int i=0;i<64;i++){auto t1=x+(r(e,6)^r(e,11)^r(e,25))+((e&f)^(~e&g))+k[i]+w[i];auto t2=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c));x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;
    }
public:
    void update(const void *data, std::size_t size) {
        const auto *p=static_cast<const std::uint8_t*>(data); length+=size;
        while(size){auto take=std::min(size,block.size()-used);std::memcpy(block.data()+used,p,take);used+=take;p+=take;size-=take;if(used==block.size()){transform(block.data());used=0;}}
    }
    std::string finish() {
        const std::uint64_t bits=length*8; block[used++]=0x80;
        if(used>56){while(used<64)block[used++]=0;transform(block.data());used=0;}
        while(used<56)block[used++]=0;
        for(int i=7;i>=0;i--)block[used++]=static_cast<std::uint8_t>(bits>>(i*8));transform(block.data());
        char out[65];for(int i=0;i<8;i++)std::snprintf(out+i*8,9,"%08x",h[i]);out[64]=0;return out;
    }
};

bool make_directories(const std::string &path) {
    for (std::size_t i = std::strlen(kModelRoot) + 1; i <= path.size(); ++i) {
        if (i != path.size() && path[i] != '/') continue;
        const auto directory = path.substr(0, i);
        if (mkdir(directory.c_str(), 0777) != 0 && errno != EEXIST) {
            storage_error("Cannot create bundle directory"); return false;
        }
    }
    return true;
}
bool fetch_preset_file(HttpSession &http, const Preset &preset, const PresetFile &item,
                       const std::string &target, std::uint64_t &completed) {
    const auto separator = target.find_last_of('/');
    if (!make_directories(target.substr(0, separator))) return false;
    const std::string part = target + ".part";
    FILE *file = std::fopen(part.c_str(), "wb");
    if (!file) { storage_error("Cannot create preset file"); return false; }
    http.close_request();
    bool ok = http.get(std::string("https://huggingface.co/") + preset.repository +
                       "/resolve/" + preset.revision + "/" + encode_path(item.source));
    Sha256 hash;
    std::array<std::uint8_t, 256 * 1024> buffer{};
    std::uint64_t done = 0;
    while (ok) {
        int count = sceHttp2ReadData(http.request, buffer.data(), buffer.size());
        if (count < 0) { set_error("Preset download", count); ok=false; break; }
        if (!count) break;
        if (done + static_cast<std::uint64_t>(count) > item.size) {
            set_status("Preset file is larger than its pinned manifest."); current.store(State::Failed); ok=false; break;
        }
        if (std::fwrite(buffer.data(), 1, count, file) != static_cast<std::size_t>(count)) {
            storage_error("Preset disk write"); ok=false; break;
        }
        hash.update(buffer.data(), count); done += count;
        completed_bytes.store(completed + done, std::memory_order_release);
    }
    if (std::fclose(file) != 0 && ok) { storage_error("Preset final disk write"); ok=false; }
    if (ok) {
        const auto digest = hash.finish();
        if (done != item.size || digest != item.sha256) {
            char error[160];
            std::snprintf(error, sizeof(error), "Verification failed: %s (%llu/%llu bytes).",
                item.path, static_cast<unsigned long long>(done), static_cast<unsigned long long>(item.size));
            std::fprintf(stderr, "[preset] %s sha256=%s expected=%s\n", error, digest.c_str(), item.sha256);
            set_status(error); current.store(State::Failed); ok=false;
        }
    }
    if (ok && rename(part.c_str(), target.c_str()) != 0) {
        storage_error("Cannot finalize preset file"); ok=false;
    }
    if (!ok) std::remove(part.c_str());
    if (ok) completed += done;
    return ok;
}
void install_preset() {
    const auto &preset = presets[worker_preset.load()];
    if (!ensure_model_root()) { storage_error("Cannot open model storage"); return; }
    HttpSession http;
    if (!http.open()) return;
    const std::string final = std::string(kModelRoot) + "/" + preset.id;
    // Hidden staging directory prevents partially downloaded bundles being discovered.
    const std::string staging = std::string(kModelRoot) + "/." + preset.id + ".download";
    std::uint64_t completed = 0;
    for (std::size_t i = 0; i < preset.count; ++i) {
        const auto &file = preset.files[i];
        const std::string target = preset.kind == 0 ? final : staging + "/" + file.path;
        if (!fetch_preset_file(http, preset, file, target, completed)) return;
    }
    if (preset.kind != 0 && rename(staging.c_str(), final.c_str()) != 0) {
        storage_error("Cannot install completed bundle"); return;
    }
    set_status(preset.kind == 0 ? "Preset verified and installed. Refreshing models."
                              : "Bundle verified and installed. Refreshing models.");
    current.store(State::Complete, std::memory_order_release);
}

void *worker_impl(void *) {
    const State action = current.load(std::memory_order_acquire);
    if (action == State::Downloading && worker_preset >= 0) {
        install_preset(); worker_done.store(true, std::memory_order_release); return nullptr;
    }
    if (action == State::Searching) {
        HttpSession http;
        if (!http.open() || !http.get("https://huggingface.co/api/models?search=" +
                                      encode_path(worker_query) +
                                      "&filter=gguf&sort=downloads&direction=-1&limit=20")) {
            worker_done.store(true, std::memory_order_release);
            return nullptr;
        }
        std::string body;
        std::array<char, 32768> chunk{};
        for (;;) {
            int n = sceHttp2ReadData(http.request, chunk.data(), chunk.size());
            if (n < 0) { set_error("Model search", n); worker_done.store(true); return nullptr; }
            if (n == 0) break;
            if (body.size() + static_cast<std::size_t>(n) > kMaxCatalogBytes) {
                set_status("Model search response is too large."); current.store(State::Failed);
                worker_done.store(true); return nullptr;
            }
            body.append(chunk.data(), static_cast<std::size_t>(n));
        }
        const Json entries = Json::parse(body, nullptr, false);
        if (entries.is_discarded() || !entries.is_array()) {
            set_status("Hugging Face returned an invalid search response.");
            current.store(State::Failed); worker_done.store(true); return nullptr;
        }
        std::vector<Item> found;
        for (const auto &entry : entries) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) continue;
            Item item;
            item.name = entry["id"].get<std::string>();
            if (!safe_repo(item.name)) continue;
            set_status("Checking repositories for GGUF files within the 7 GiB download limit...");
            std::vector<Item> eligible;
            if (!read_catalog(item.name, &eligible)) {
                current.store(State::Searching, std::memory_order_release);
                continue;
            }
            if (eligible.empty()) continue;
            if (entry.contains("downloads") && entry["downloads"].is_number_integer()) {
                const auto count = entry["downloads"].get<std::int64_t>();
                if (count > 0) item.size = static_cast<std::uint64_t>(count);
            }
            found.push_back(std::move(item));
            if (found.size() == 8) break;
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            items = std::move(found);
            std::snprintf(current_status, sizeof(current_status),
                          "%zu repositories with GGUF files up to 7 GiB found.", items.size());
        }
        current.store(State::SearchReady, std::memory_order_release);
        worker_done.store(true, std::memory_order_release);
        return nullptr;
    }
    if (action == State::Loading) {
        std::vector<Item> found;
        if (read_catalog(worker_repo, &found)) {
            std::lock_guard<std::mutex> lock(state_mutex); items=std::move(found);
            if (items.empty())
                std::snprintf(current_status, sizeof(current_status),
                              "No verified single GGUF files within the 7 GiB download limit.");
            else
                std::snprintf(current_status, sizeof(current_status),
                              "%zu GGUF files within the 7 GiB limit.", items.size());
            current.store(State::Ready, std::memory_order_release);
        }
        worker_done.store(true, std::memory_order_release);
        return nullptr;
    } else {
        HttpSession http;
        if (!http.open()) { worker_done.store(true, std::memory_order_release); return nullptr; }
        Item item;
        std::string repo;
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (worker_index >= items.size()) { current.store(State::Failed); worker_done.store(true); return nullptr; }
            item=items[worker_index]; repo=worker_repo;
        }
        if (!ensure_model_root()) { storage_error("Cannot open model storage"); worker_done.store(true); return nullptr; }
        const std::string final_path=std::string(kModelRoot)+"/"+installed_name(repo,item.name);
        const std::string part_path=final_path+".part";
        FILE *file=std::fopen(part_path.c_str(),"wb");
        if (!file) { storage_error("Cannot create model file"); worker_done.store(true); return nullptr; }
        const std::string url="https://huggingface.co/"+repo+"/resolve/main/"+encode_path(item.name);
        bool ok=http.get(url);
        std::uint64_t expected=item.size;
        Sha256 hash;
        std::array<std::uint8_t, 1024*1024> buffer{};
        std::uint64_t done=0;
        while(ok){int n=sceHttp2ReadData(http.request,buffer.data(),buffer.size());if(n<0){set_error("Model download",n);ok=false;break;}if(n==0)break;
            if(done+static_cast<std::uint64_t>(n)>expected){set_error("Model size check",-1);ok=false;break;}
            if(std::fwrite(buffer.data(),1,n,file)!=static_cast<std::size_t>(n)){storage_error("Disk write");ok=false;break;}
            hash.update(buffer.data(),n);done+=n;completed_bytes.store(done,std::memory_order_release);
        }
        if (std::fclose(file) != 0 && ok) { storage_error("Final disk write"); ok=false; }
        total_bytes.store(expected,std::memory_order_release);
        if(ok&&(done!=expected||hash.finish()!=item.sha256)){set_status("Download size or SHA-256 check failed; partial file removed.");current.store(State::Failed);ok=false;}
        if(ok&&rename(part_path.c_str(),final_path.c_str())!=0){set_status("Could not finalize downloaded model file.");current.store(State::Failed);ok=false;}
        if(!ok) std::remove(part_path.c_str());
        if(ok){char text[160];std::snprintf(text,sizeof(text),"Verified %s (%llu MiB). Restart not needed; refreshing models.",installed_name(repo,item.name).c_str(),static_cast<unsigned long long>(done/(1024*1024)));set_status(text);current.store(State::Complete,std::memory_order_release);}
    }
    worker_done.store(true, std::memory_order_release);
    return nullptr;
}
void *worker(void *argument) {
    try {
        return worker_impl(argument);
    } catch (...) {
        set_status("Downloader stopped after an invalid catalog or unexpected error.");
        current.store(State::Failed, std::memory_order_release);
        worker_done.store(true, std::memory_order_release);
        return nullptr;
    }
}
bool start() {
    worker_done.store(false,std::memory_order_release); worker_thread=nullptr;
    pthread_attr_t attributes; int rc=pthread_attr_init(&attributes);
    const bool attributes_ready = rc == 0;
    if(rc==0)rc=pthread_attr_setstacksize(&attributes,4*1024*1024);
    if(rc==0)rc=scePthreadCreate(&worker_thread,&attributes,worker,nullptr,"prosperoai-model-download");
    if(attributes_ready) pthread_attr_destroy(&attributes);
    if(rc!=0){set_error("Downloader thread",rc);worker_done.store(true);return false;}
    return true;
}
}
bool preset_installed(std::size_t index) {
    if (index >= preset_count) return false;
    const auto &preset = presets[index];
    for (std::size_t i = 0; i < preset.count; ++i) {
        const std::string path = std::string(kModelRoot) + "/" + preset.id +
                                (preset.kind == 0 ? "" : std::string("/") + preset.files[i].path);
        struct stat file{};
        if (stat(path.c_str(), &file) != 0 || !S_ISREG(file.st_mode) ||
            static_cast<std::uint64_t>(file.st_size) != preset.files[i].size) return false;
    }
    return true;
}
bool download_preset(std::size_t index) {
    poll();
    std::lock_guard<std::mutex> job_lock(job_mutex);
    if (index >= preset_count || worker_thread || preset_installed(index)) return false;
    worker_preset = static_cast<int>(index);
    completed_bytes.store(0); total_bytes.store(presets[index].size);
    current.store(State::Downloading); set_status("Downloading pinned preset bundle...");
    return start();
}
void poll() {
    std::lock_guard<std::mutex> job_lock(job_mutex);
    if(worker_thread && worker_done.load(std::memory_order_acquire)) {
        scePthreadJoin(worker_thread,nullptr);worker_thread=nullptr;

    }
}
State state(){return current.load(std::memory_order_acquire);}
int active_preset() { return state() == State::Downloading ? worker_preset.load() : -1; }
void progress(std::uint64_t *completed, std::uint64_t *total) {
    if (completed) *completed = completed_bytes.load(std::memory_order_acquire);
    if (total) *total = total_bytes.load(std::memory_order_acquire);
}
void status(char *out,std::size_t cap){if(!cap)return;std::lock_guard<std::mutex> lock(state_mutex);std::snprintf(out,cap,"%s",current_status);if(state()==State::Downloading){auto total=total_bytes.load();auto done=completed_bytes.load();if(total){char text[160];std::snprintf(text,sizeof(text),"Downloading: %llu / %llu MiB (%u%%)",static_cast<unsigned long long>(done/(1024*1024)),static_cast<unsigned long long>(total/(1024*1024)),static_cast<unsigned>((done*100)/total));std::snprintf(out,cap,"%s",text);}}}
std::size_t candidate_count(){std::lock_guard<std::mutex> lock(state_mutex);return items.size();}
bool candidate(std::size_t i,Candidate*out){if(!out)return false;std::lock_guard<std::mutex> lock(state_mutex);if(i>=items.size())return false;std::snprintf(out->name,sizeof(out->name),"%s",items[i].name.c_str());out->size=items[i].size;return true;}
bool search(const char *query){poll();std::lock_guard<std::mutex> job_lock(job_mutex);if(!query||state()==State::Searching||state()==State::Loading||state()==State::Downloading||worker_thread)return false;std::string value(query);if(value.size()<2||value.size()>96){set_status("Enter at least two characters for model search.");current.store(State::Failed);return false;}for(unsigned char c:value)if(!(std::isalnum(c)||std::isspace(c)||c=='-'||c=='_'||c=='.'||c=='+')){set_status("Use letters, numbers, spaces, dots, dashes or underscores.");current.store(State::Failed);return false;}worker_query=value;{std::lock_guard<std::mutex> lock(state_mutex);items.clear();}current.store(State::Searching);set_status("Searching public GGUF repositories...");return start();}
bool browse(const char *repo){poll();std::lock_guard<std::mutex> job_lock(job_mutex);if(!repo||state()==State::Loading||state()==State::Downloading||worker_thread)return false;std::string value(repo);if(!safe_repo(value)){set_status("Enter a repository as owner/name.");current.store(State::Failed);return false;}worker_repo=value;{std::lock_guard<std::mutex> lock(state_mutex);items.clear();}current.store(State::Loading);set_status("Connecting to Hugging Face over HTTPS...");return start();}
bool download(std::size_t i){poll();std::lock_guard<std::mutex> job_lock(job_mutex);if(state()!=State::Ready||worker_thread)return false;worker_preset=-1;{std::lock_guard<std::mutex> lock(state_mutex);if(i>=items.size())return false;worker_index=i;}completed_bytes.store(0);total_bytes.store(items[i].size);current.store(State::Downloading);set_status("Starting model download...");return start();}
}
#endif // PS5_LLAMA_VULKAN
