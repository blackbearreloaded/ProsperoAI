// Exercises fallback allocations used by Mesa's realloc/usable-size calls.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <limits>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <array>

std::atomic<unsigned> initialization_count{0};

extern "C"
{
    void *__wrap_malloc(std::size_t);
    void *__wrap_realloc(void *, std::size_t);
    std::size_t __wrap_malloc_usable_size(const void *);
    void __wrap_free(void *);
    int __wrap_sceAgcInit(std::uint32_t);
    int __real_sceAgcInit(std::uint32_t flags)
    {
        assert(flags == 8);
        ++initialization_count;
        usleep(1000);
        return 7;
    }
    int sceKernelUsleep(std::uint32_t time)
    {
        return usleep(time);
    }
    void *mmap(void *address, std::size_t size, int protection, int, int, off_t)
    {
        return reinterpret_cast<void *>(
            syscall(SYS_mmap, address, size, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    }
    void *__real_malloc(std::size_t size)
    {
        return size >= 4096 ? nullptr : std::malloc(size);
    }
    void *__real_calloc(std::size_t count, std::size_t size)
    {
        return std::calloc(count, size);
    }
    void *__real_realloc(void *address, std::size_t size)
    {
        return std::realloc(address, size);
    }
    void __real_free(void *address)
    {
        std::free(address);
    }
    int __real_posix_memalign(void **address, std::size_t alignment, std::size_t size)
    {
        return posix_memalign(address, alignment, size);
    }
    std::size_t __real_malloc_usable_size(const void *address)
    {
        return malloc_usable_size(const_cast<void *>(address));
    }
    std::int64_t sceKernelGetDirectMemorySize()
    {
        return 0;
    }
    int sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int,
                                      std::int64_t *)
    {
        return -1;
    }
    int sceKernelMapDirectMemory(void **, std::size_t, int, int, std::int64_t, std::size_t)
    {
        return -1;
    }
    int sceKernelMapNamedFlexibleMemory(void **, std::size_t, int, int, const char *)
    {
        return -1;
    }
    int sceKernelMunmap(void *address, std::size_t size)
    {
        return munmap(address, size);
    }
    int sceKernelReleaseDirectMemory(std::int64_t, std::size_t)
    {
        return 0;
    }
}

int main()
{
    std::array<std::thread, 8> threads;
    for (auto &thread : threads)
        thread = std::thread([] { assert(__wrap_sceAgcInit(8) == 7); });
    for (auto &thread : threads)
        thread.join();
    assert(initialization_count == 1);
    auto *first = static_cast<unsigned char *>(__wrap_malloc(8192));
    assert(first && __wrap_malloc_usable_size(first) == 8192);
    std::memset(first, 0x5a, 8192);
    auto *grown = static_cast<unsigned char *>(__wrap_realloc(first, 32768));
    assert(grown && __wrap_malloc_usable_size(grown) == 32768);
    for (unsigned i = 0; i < 8192; ++i)
        assert(grown[i] == 0x5a);
    assert(!__wrap_realloc(grown, std::numeric_limits<std::size_t>::max()));
    assert(grown[8191] == 0x5a);
    auto *small = static_cast<unsigned char *>(__wrap_realloc(grown, 16));
    assert(small && small[15] == 0x5a);
    __wrap_free(small);
    assert(__wrap_malloc_usable_size(nullptr) == 0);
    void *last = __wrap_realloc(nullptr, 8192);
    assert(last && !__wrap_realloc(last, 0));
}
