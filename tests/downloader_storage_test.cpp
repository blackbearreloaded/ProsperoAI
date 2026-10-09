// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <pthread.h>
static int mounted_stat(const char *path, struct stat *result)
{
    assert(std::strcmp(path, "/data/homebrew/prosperoai/models") == 0);
    result->st_mode = S_IFDIR | 0777;
    return 0;
}
static int forbidden_parent_mkdir(const char *, mode_t)
{
    assert(false && "Mounted storage must not depend on access to sandboxed parents");
    errno = EACCES;
    return -1;
}
#define stat(...) mounted_stat(__VA_ARGS__)
#define mkdir(...) forbidden_parent_mkdir(__VA_ARGS__)
#define PS5_LLAMA_VULKAN
#include "../vulkan/model_downloader_ps5.cpp"
#undef stat
#undef mkdir
static std::vector<std::string> requested;
static std::string response_headers;
extern "C"
{
    int sceHttp2CreateRequestWithURL(int, const char *, const char *url, std::uint64_t)
    {
        requested.emplace_back(url);
        return 42;
    }
    int sceHttp2DeleteRequest(int)
    {
        return 0;
    }
    int sceHttp2SetAutoRedirect(int, int enabled)
    {
        assert(!enabled);
        return 0;
    }
    int sceHttp2SendRequest(int, const void *, std::size_t)
    {
        return 0;
    }
    int sceHttp2GetStatusCode(int, int *status)
    {
        *status = requested.size() < 3 ? 302 : 200;
        return 0;
    }
    int sceHttp2GetAllResponseHeaders(int, char **out, std::size_t *size)
    {
        response_headers = requested.size() == 1
                               ? "HTTP/2 302\r\nLocation: /api/resolved\r\n"
                               : "HTTP/2 302\r\nlocation: https://cdn.hf.co/model.bin\r\n";
        *out = response_headers.data();
        *size = response_headers.size();
        return 0;
    }
    int sceHttp2DeleteTemplate(int)
    {
        return 0;
    }
    int sceHttp2Term(int)
    {
        return 0;
    }
    int sceSslTerm(int)
    {
        return 0;
    }
    int sceNetPoolDestroy(int)
    {
        return 0;
    }
}
int main()
{
    using namespace prospero_model_download;
    assert(ensure_model_root());
    HttpSession session;
    assert(session.get("https://huggingface.co/model/resolve/main/model.bin"));
    assert(requested.size() == 3);
    assert(requested[1] == "https://huggingface.co/api/resolved");
    assert(requested[2] == "https://cdn.hf.co/model.bin");
    Sha256 hash;
    hash.update("a", 1);
    hash.update("bc", 2);
    assert(hash.finish() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(!safe_filename("BF16/model-00001-of-00002.gguf"));
    assert(!safe_filename("../model.gguf"));
    assert(!safe_filename("mmproj-Q4_0.gguf"));
    assert(safe_filename("model-Q4_0.gguf"));
    bool kinds[4]{};
    for (const auto &preset : presets)
    {
        assert(preset.kind >= 0 && preset.kind < 4);
        kinds[preset.kind] = true;
        assert(preset.size > 0 && preset.size <= kMaxModelBytes);
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < preset.count; ++i)
            total += preset.files[i].size;
        assert(total == preset.size);
    }
    for (bool present : kinds)
        assert(present);
}
