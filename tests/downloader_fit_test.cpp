// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host unit test for prospero_model_download::estimate_fit(). Links the whole
// model_downloader_ps5.cpp (it is guarded by PS5_LLAMA_VULKAN and estimate_fit is defined
// there, not in the header), following the same stand-in pattern as
// downloader_storage_test.cpp so the networking/thread externs it references resolve.
#include <cassert>
#include <cstring>
#define PS5_LLAMA_VULKAN
#include "../vulkan/model_downloader_ps5.cpp"

struct prospero_https
{
    int unused;
};
extern "C"
{
    prospero_https *prospero_https_open(void)
    {
        return nullptr;
    }
    void prospero_https_close(prospero_https *)
    {
    }
    int prospero_https_get_from(prospero_https *, const char *, unsigned long long, long *status)
    {
        *status = 404;
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
    int prospero_https_receive_buffer(void)
    {
        return 0;
    }
    int scePthreadJoin(void *, void **)
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
// No Vulkan device on the host; estimate_fit() falls back to its known-heap constant.
std::uint64_t gpt_runtime_vulkan_heap_bytes()
{
    return 0;
}

int main()
{
    using prospero_model_download::estimate_fit;
    using prospero_model_download::Fit;
    assert(estimate_fit(0) == Fit::Fits);
    assert(estimate_fit(3ULL * 1024 * 1024 * 1024) == Fit::Fits); // Mistral Q4_0-sized
    assert(estimate_fit(8ULL * 1024 * 1024 * 1024) ==
           Fit::Tight); // spec's Q2_g64 7.59 GB plus margin
    assert(estimate_fit(10ULL * 1024 * 1024 * 1024) == Fit::Tight);
    assert(estimate_fit(11ULL * 1024 * 1024 * 1024) ==
           Fit::DoesNotFit);                        // above the 10.7 GiB ceiling
    assert(estimate_fit(~0ULL) == Fit::DoesNotFit); // no overflow/UB at the top of the range
}
