// SPDX-License-Identifier: GPL-3.0-or-later
#include <sys/stat.h>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <pthread.h>
static int mounted_stat(const char *path, struct stat *result)
{
    assert(std::strcmp(path, "/data/prosperoai/models") == 0);
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
static long answer_status = 200;
struct prospero_https
{
    int unused;
};
extern "C"
{
    prospero_https *prospero_https_open(void)
    {
        static prospero_https session;
        return &session;
    }
    void prospero_https_close(prospero_https *)
    {
    }
    int prospero_https_get(prospero_https *, const char *url, long *status)
    {
        requested.emplace_back(url);
        *status = answer_status;
        return 0;
    }
    int prospero_https_read(prospero_https *, void *, std::size_t)
    {
        return 0;
    }
    const char *prospero_https_error(const prospero_https *)
    {
        return "test";
    }
    void prospero_https_set_stop(prospero_https *, prospero_https_stop, void *)
    {
    }
    int scePthreadJoin(void *, void **)
    {
        return 0;
    }
    int prospero_https_receive_buffer(void)
    {
        return 0;
    }
}
namespace prospero::debug
{
void line(const char *, const char *, ...)
{
}
} // namespace prospero::debug
int main()
{
    using namespace prospero_model_download;
    assert(ensure_model_root());
    HttpSession session;
    assert(session.get("https://huggingface.co/model/resolve/main/model.bin"));
    assert(requested.size() == 1);
    // An answer that is not the file is a failure the page reports, with its status.
    answer_status = 404;
    assert(!session.get("https://huggingface.co/model/resolve/main/missing.bin"));
    assert(current.load() == State::Failed);
    assert(std::strstr(current_status, "HTTP 404") != nullptr);
    // Cancelling: only a running download can be cancelled; its partial files go, the
    // repository's file list stays.
    assert(!cancel() && !cancelling());
    int placeholder = 0;
    worker_thread = &placeholder;
    current.store(State::Downloading);
    total_bytes.store(200);
    completed_bytes.store(50);
    assert(cancel() && cancelling() && cancel_pending());
    char line[160]{};
    status(line, sizeof(line));
    assert(std::strcmp(line, "Cancelling the download...") == 0);
    char staging[] = "/tmp/prospero-cancel-XXXXXX";
    assert(mkdtemp(staging));
    const std::string nested = std::string(staging) + "/weights";
    assert(::mkdir(nested.c_str(), 0777) == 0);
    for (const std::string &file : {std::string(staging) + "/model.json.part", nested + "/a.bin"})
    {
        std::FILE *made = std::fopen(file.c_str(), "wb");
        assert(made && std::fclose(made) == 0);
    }
    remove_tree(staging);
    assert(access(staging, F_OK) != 0);
    finish_cancelled(State::Ready);
    worker_thread = nullptr;
    cancel_requested.store(false);
    assert(current.load() == State::Ready && !cancelling());
    status(line, sizeof(line));
    assert(std::strstr(line, "cancelled") != nullptr);
    std::uint64_t completed_now = 1, total_now = 1;
    progress(&completed_now, &total_now);
    assert(completed_now == 0 && total_now == 0);
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
