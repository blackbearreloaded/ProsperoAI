// Real model discovery, with paged directory reads and remapped host fixtures.
#include "gpt_runtime.hpp"
#include "model_paths.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>

namespace
{
std::string root;
DIR *directory = nullptr;
unsigned pages = 0;
} // namespace
extern "C"
{
    std::FILE *__real_fopen(const char *, const char *);
    std::FILE *__wrap_fopen(const char *path, const char *mode)
    {
        const auto mapped = std::string(path).find(std::string(prospero::model_root()) + "/") == 0
                                ? root + (path + std::strlen(prospero::model_root()))
                                : std::string(path);
        return __real_fopen(mapped.c_str(), mode);
    }
    int sceKernelOpen(const char *path, int, int)
    {
        assert(std::strcmp(path, prospero::model_root()) == 0);
        directory = opendir(root.c_str());
        return directory ? 1 : -1;
    }
    int sceKernelClose(int)
    {
        return closedir(directory);
    }
    int sceKernelGetdents(int, char *buffer, int capacity)
    {
        ++pages;
        int written = 0;
        // Deliberately return only two records per call, like a small directory buffer.
        for (int i = 0; i < 2; ++i)
        {
            dirent *entry = readdir(directory);
            if (!entry)
                break;
            assert(written + entry->d_reclen <= capacity);
            std::memcpy(buffer + written, entry, entry->d_reclen);
            written += entry->d_reclen;
        }
        return written;
    }
    int sceKernelDebugOutText(int, const char *)
    {
        return 0;
    }
    int ps5_compute_select_model_files(const char *, const char *)
    {
        return 0;
    }
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    root = argv[1];
    assert(gpt_runtime_model_count() == 24);
    assert(pages > 12);
    assert(std::strcmp(gpt_runtime_model_id(0), "variant-00") == 0);
    assert(std::strcmp(gpt_runtime_model_id(23), "variant-23") == 0);
    assert(std::strcmp(gpt_runtime_model_name(23), "Local variation 23") == 0);
    assert(gpt_runtime_select_model(23));
    assert(!gpt_runtime_select_model(24));
    assert(gpt_runtime_selected_model() == 23);
}
