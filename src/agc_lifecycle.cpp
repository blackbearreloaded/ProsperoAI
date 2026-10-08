// OpenGL and inference share process-wide AGC initialization.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <atomic>
#include <cstdint>
extern "C" int __real_sceAgcInit(std::uint32_t);
extern "C" int sceKernelUsleep(std::uint32_t);
extern "C" int __wrap_sceAgcInit(std::uint32_t flags)
{
    static std::atomic<int> state{0};
    static int result = 0;
    int expected = 0;
    if (state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
    {
        result = __real_sceAgcInit(flags);
        state.store(2, std::memory_order_release);
    }
    else
        while (state.load(std::memory_order_acquire) != 2)
            sceKernelUsleep(100);
    return result;
}
