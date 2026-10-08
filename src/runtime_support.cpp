#include <SDL2/SDL.h>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <pthread.h>
extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int ps5_agc_backend_reserve(void);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void *mmap(void *address, std::size_t length, int protection, int flags, int descriptor,
                      long offset);
extern "C" int munmap(void *address, std::size_t length);
extern "C" void *__dso_handle = nullptr;
#ifndef PS5_LLAMA_VULKAN
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};
#endif

namespace
{

constexpr std::size_t kMappedAllocationThreshold = 64 * 1024;
constexpr std::uint64_t kAllocationMagic = UINT64_C(0x524144494F4D454D);
constexpr int kProtectionReadWrite = 3;
constexpr int kMapPrivateAnonymous = 0x1002;

struct alignas(std::max_align_t) AllocationHeader
{
    std::uint64_t magic;
    std::size_t requested_size;
    std::size_t mapped_size;
};

void *AllocateTracked(std::size_t size)
{
    if (size == 0)
        size = 1;
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader))
        return nullptr;

    const std::size_t total = sizeof(AllocationHeader) + size;
    AllocationHeader *header = nullptr;
    std::size_t mapped_size = 0;
    if (size >= kMappedAllocationThreshold)
    {
        mapped_size = (total + 0x3fff) & ~std::size_t(0x3fff);
        void *mapping = mmap(reinterpret_cast<void *>(0x600000000ULL), mapped_size,
                             kProtectionReadWrite, kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void *>(-1))
        {
            header = static_cast<AllocationHeader *>(mapping);
        }
    }
    else
    {
        header = static_cast<AllocationHeader *>(std::malloc(total));
    }
    if (!header)
        return nullptr;

    header->magic = kAllocationMagic;
    header->requested_size = size;
    header->mapped_size = mapped_size;
    return header + 1;
}

void FreeTracked(void *allocation) noexcept
{
    if (!allocation)
        return;
    auto *header = static_cast<AllocationHeader *>(allocation) - 1;
    // SDL can retain small allocations made by its original allocator before
    // custom memory functions are installed. Those remain libc-owned.
    if (header->magic != kAllocationMagic)
    {
        std::free(allocation);
        return;
    }
    if (header->mapped_size != 0)
    {
        munmap(header, header->mapped_size);
    }
    else
    {
        std::free(header);
    }
}

void *CallocTracked(std::size_t count, std::size_t size)
{
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
        return nullptr;
    const std::size_t total = count * size;
    void *allocation = AllocateTracked(total);
    if (allocation)
        std::memset(allocation, 0, total);
    return allocation;
}

void *ReallocTracked(void *allocation, std::size_t size)
{
    if (!allocation)
        return AllocateTracked(size);
    if (size == 0)
    {
        FreeTracked(allocation);
        return nullptr;
    }

    auto *old_header = static_cast<AllocationHeader *>(allocation) - 1;
    if (old_header->magic != kAllocationMagic)
        std::abort();
    void *replacement = AllocateTracked(size);
    if (!replacement)
        return nullptr;
    std::memcpy(replacement, allocation,
                old_header->requested_size < size ? old_header->requested_size : size);
    FreeTracked(allocation);
    return replacement;
}

} // namespace

#ifdef PS5_LLAMA_VULKAN
#define pthread_once prospero_pthread_once
#define strtof prospero_strtof
#define fseek prospero_fseek
#define ftell prospero_ftell
#define strcasestr prospero_strcasestr
#endif

extern "C" int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT)
        return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT)
    {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" float strtof(const char *value, char **end)
{
    return static_cast<float>(strtod(value, end));
}

extern "C" int fseek(std::FILE *file, long offset, int origin)
{
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE *file)
{
    return static_cast<long>(ftello(file));
}

extern "C" char *strcasestr(const char *haystack, const char *needle)
{
    if (!*needle)
        return const_cast<char *>(haystack);
    for (; *haystack; ++haystack)
    {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n)
        {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc)
                break;
            ++h;
            ++n;
        }
        if (!*n)
            return const_cast<char *>(haystack);
    }
    return nullptr;
}

void prospero_setup_sdl_memory()
{
    SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked);
}
