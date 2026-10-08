// Exercises the application allocator: the interface heap the OpenGL runtime lives on,
// and the fallback allocations behind it.
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
#include <pthread.h>

std::atomic<unsigned> initialization_count{0};

// A stand-in for the system's mspace allocator: blocks handed out in order from the region
// it was given, each remembering the size asked for.
struct FakeSpace
{
    unsigned char *base = nullptr;
    std::size_t size = 0, used = 0;
} fake_space;
bool in_interface_heap(const void *address)
{
    const auto *byte = static_cast<const unsigned char *>(address);
    return fake_space.base && byte >= fake_space.base && byte < fake_space.base + fake_space.size;
}

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
    void *scePthreadSelf()
    {
        return reinterpret_cast<void *>(pthread_self());
    }
    void ps5SetInterfaceThread() noexcept;
    void ps5SetDirectFallback(int) noexcept;
    void *sceLibcMspaceCreate(const char *, void *base, std::size_t size, unsigned)
    {
        fake_space = {static_cast<unsigned char *>(base), size, 0};
        return &fake_space;
    }
    void *sceLibcMspaceMalloc(void *, std::size_t size)
    {
        const std::size_t need = (size + 16 + 15) & ~std::size_t(15);
        if (need > fake_space.size - fake_space.used)
            return nullptr;
        unsigned char *block = fake_space.base + fake_space.used;
        fake_space.used += need;
        std::memcpy(block, &size, sizeof(size));
        return block + 16;
    }
    std::size_t sceLibcMspaceMallocUsableSize(const void *address)
    {
        std::size_t size = 0;
        std::memcpy(&size, static_cast<const unsigned char *>(address) - 16, sizeof(size));
        return size;
    }
    void *sceLibcMspaceCalloc(void *space, std::size_t count, std::size_t size)
    {
        void *address = sceLibcMspaceMalloc(space, count * size);
        if (address)
            std::memset(address, 0, count * size);
        return address;
    }
    void *sceLibcMspaceRealloc(void *space, void *address, std::size_t size)
    {
        void *next = sceLibcMspaceMalloc(space, size);
        if (next)
        {
            const std::size_t kept = sceLibcMspaceMallocUsableSize(address);
            std::memcpy(next, address, kept < size ? kept : size);
        }
        return next;
    }
    void sceLibcMspaceFree(void *, void *)
    {
    }
    int sceLibcMspacePosixMemalign(void *space, void **address, std::size_t alignment,
                                   std::size_t size)
    {
        if (alignment > 16)
            return 12;
        *address = sceLibcMspaceMalloc(space, size);
        return *address ? 0 : 12;
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
    // What the libc heap refuses is served by the interface heap first.
    auto *first = static_cast<unsigned char *>(__wrap_malloc(8192));
    assert(first && __wrap_malloc_usable_size(first) == 8192 && in_interface_heap(first));
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

    // Off the interface's thread a large block never enters the interface heap: it is
    // mapped on its own, as the model runtimes expect, and can be resized and freed.
    auto *tensor = static_cast<unsigned char *>(__wrap_malloc(3u << 20));
    assert(tensor && !in_interface_heap(tensor) && __wrap_malloc_usable_size(tensor) == 3u << 20);
    tensor[0] = 0x33;
    auto *shrunk = static_cast<unsigned char *>(__wrap_realloc(tensor, 4096));
    assert(shrunk && shrunk[0] == 0x33 && in_interface_heap(shrunk));
    // A block that outgrows the heap's limit moves out of it with its content.
    auto *moved = static_cast<unsigned char *>(__wrap_realloc(shrunk, 3u << 20));
    assert(moved && moved[0] == 0x33 && !in_interface_heap(moved));
    __wrap_free(moved);

    // The interface's own thread keeps its large blocks (font atlases, pictures) in the heap.
    ps5SetInterfaceThread();
    void *atlas = __wrap_malloc(26u << 20);
    assert(atlas && in_interface_heap(atlas));
    __wrap_free(atlas);
    // While a model that uses the direct arena is selected, the interface still never
    // reaches that arena (here it has no direct memory at all); other threads do.
    ps5SetDirectFallback(1);
    void *huge = __wrap_malloc(100u << 20);
    assert(huge && !in_interface_heap(huge));
    __wrap_free(huge);
    std::thread([] { assert(__wrap_malloc(3u << 20) == nullptr); }).join();
    ps5SetDirectFallback(0);
}
