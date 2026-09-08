#define PS5_WRAP_MALLOC
/*
 * Generate-only C++ allocation bridge for stable-diffusion.cpp.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <atomic>
#include <sys/mman.h>

extern "C"
{
    void *malloc(std::size_t size);
    void free(void *address);
    int posix_memalign(void **address, std::size_t alignment, std::size_t size);
    void __real_free(void *address);
    int __real_posix_memalign(void **address, std::size_t alignment, std::size_t size);
#ifdef PS5_WRAP_MALLOC
    void *__real_malloc(std::size_t size);
    void *__real_calloc(std::size_t count, std::size_t size);
#endif
    std::int64_t sceKernelGetDirectMemorySize(void);
    std::int32_t sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t,
                                               int, std::int64_t *);
    std::int32_t sceKernelMapDirectMemory(void **, std::size_t, int, int, std::int64_t,
                                          std::size_t);
    std::int32_t sceKernelMapNamedFlexibleMemory(void **, std::size_t, int, int, const char *);
    std::int32_t sceKernelMunmap(void *, std::size_t);
    std::int32_t sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
}

namespace
{
constexpr std::size_t direct_alignment = 0x4000;
constexpr std::size_t direct_arena_size = 4ULL * 1024 * 1024 * 1024;
constexpr std::size_t small_mapped_allocation_limit = 2ULL * 1024 * 1024;
constexpr std::size_t mapped_allocation_slots = 256;
constexpr std::uint64_t direct_allocation_magic = 0x505335414c4c4f43ULL;

struct alignas(std::max_align_t) DirectAllocation
{
    std::uint64_t magic;
    std::size_t previous_used;
    DirectAllocation *previous;
    bool released;
};

struct MappedAllocation
{
    void *mapping;
    void *address;
    std::size_t mapped_size;
};

// ponytail: the generator uses one model-loading/inference thread; add a lock
// only if concurrent allocation becomes measurable.
void *direct_arena{};
std::int64_t direct_arena_physical{-1};
std::size_t direct_arena_used{};
DirectAllocation *direct_tail{};
MappedAllocation mapped_allocations[mapped_allocation_slots]{};
std::atomic_flag direct_arena_lock = ATOMIC_FLAG_INIT;
std::atomic<bool> direct_fallback_enabled{false};

struct DirectArenaLock
{
    DirectArenaLock() noexcept
    {
        while (direct_arena_lock.test_and_set(std::memory_order_acquire))
            __asm__ volatile("pause");
    }
    ~DirectArenaLock()
    {
        direct_arena_lock.clear(std::memory_order_release);
    }
};

[[nodiscard]] void *allocate_direct(std::size_t size, std::size_t alignment) noexcept;

[[nodiscard]] void *allocate_mapped(std::size_t size, std::size_t alignment, bool cpu_only) noexcept
{
    if (size > direct_arena_size || alignment > direct_alignment ||
        size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
        return nullptr;

    DirectArenaLock lock;
    MappedAllocation *slot = nullptr;
    for (auto &allocation : mapped_allocations)
    {
        if (allocation.mapping == nullptr)
        {
            slot = &allocation;
            break;
        }
    }
    if (!slot)
        return nullptr;

    const std::size_t required = size + alignment - 1;
    const std::size_t mapped_size = (required + direct_alignment - 1) & ~(direct_alignment - 1);
    void *mapping =
        cpu_only ? mmap(reinterpret_cast<void *>(0x600000000ULL), mapped_size, 0x03, 0x1002, -1, 0)
                 : nullptr;
    if (cpu_only)
    {
        if (mapping == MAP_FAILED || (reinterpret_cast<std::uintptr_t>(mapping) >> 32) == 2u)
        {
            if (mapping != MAP_FAILED)
                sceKernelMunmap(mapping, mapped_size);
            return nullptr;
        }
    }
    else if (sceKernelMapNamedFlexibleMemory(&mapping, mapped_size, 0x03, 0, "ProsperoAI small") !=
             0)
    {
        return nullptr;
    }
    const auto aligned = (reinterpret_cast<std::uintptr_t>(mapping) + alignment - 1) &
                         ~(static_cast<std::uintptr_t>(alignment) - 1);
    *slot = {mapping, reinterpret_cast<void *>(aligned), mapped_size};
    return slot->address;
}

[[nodiscard]] void *allocate_fallback(std::size_t size, std::size_t alignment) noexcept
{
    const bool use_direct = direct_fallback_enabled.load(std::memory_order_acquire);
    if (!use_direct || size <= small_mapped_allocation_limit)
    {
        if (void *address = allocate_mapped(size, alignment, !use_direct))
            return address;
    }
    return use_direct ? allocate_direct(size, alignment) : nullptr;
}

bool release_mapped(void *address) noexcept
{
    DirectArenaLock lock;
    for (auto &allocation : mapped_allocations)
    {
        if (allocation.address != address)
            continue;
        sceKernelMunmap(allocation.mapping, allocation.mapped_size);
        allocation = {};
        return true;
    }
    return false;
}

[[nodiscard]] void *allocate_direct(std::size_t size, std::size_t alignment) noexcept
{
    DirectArenaLock lock;
    if (alignment < alignof(DirectAllocation))
        alignment = alignof(DirectAllocation);
    if (alignment > direct_alignment ||
        size > std::numeric_limits<std::size_t>::max() - sizeof(DirectAllocation) - (alignment - 1))
        return nullptr;

    if (direct_arena == nullptr)
    {
        const std::int64_t total = sceKernelGetDirectMemorySize();
        std::int64_t physical = -1;
        if (total <= 0 ||
            sceKernelAllocateDirectMemory(0, total, direct_arena_size, direct_alignment, 12,
                                          &physical) != 0 ||
            sceKernelMapDirectMemory(&direct_arena, direct_arena_size, 0x33, 0, physical,
                                     direct_alignment) != 0)
        {
            if (physical >= 0)
                sceKernelReleaseDirectMemory(physical, direct_arena_size);
            direct_arena = nullptr;
            return nullptr;
        }
        direct_arena_physical = physical;
    }

    const std::size_t previous_used = direct_arena_used;
    const std::size_t offset =
        (previous_used + sizeof(DirectAllocation) + alignment - 1) & ~(alignment - 1);
    if (offset > direct_arena_size || size > direct_arena_size - offset)
        return nullptr;
    auto *allocation = reinterpret_cast<DirectAllocation *>(
        static_cast<unsigned char *>(direct_arena) + offset - sizeof(DirectAllocation));
    *allocation = {direct_allocation_magic, previous_used, direct_tail, false};
    direct_tail = allocation;
    direct_arena_used = offset + size;
    return static_cast<unsigned char *>(direct_arena) + offset;
}

bool release_direct(void *address) noexcept
{
    DirectArenaLock lock;
    if (direct_arena == nullptr)
        return false;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto begin = reinterpret_cast<std::uintptr_t>(direct_arena);
    if (value < begin || value >= begin + direct_arena_size)
        return false;

    auto *allocation = reinterpret_cast<DirectAllocation *>(static_cast<unsigned char *>(address) -
                                                            sizeof(DirectAllocation));
    if (allocation->magic != direct_allocation_magic)
        return true;
    allocation->released = true;
    while (direct_tail != nullptr && direct_tail->released)
    {
        DirectAllocation *previous = direct_tail->previous;
        direct_arena_used = direct_tail->previous_used;
        direct_tail->magic = 0;
        direct_tail = previous;
    }
    return true;
}

[[nodiscard]] void *allocate(std::size_t size) noexcept
{
    size = size == 0 ? 1 : size;
    if (void *address = malloc(size))
        return address;
    return allocate_fallback(size, alignof(std::max_align_t));
}

[[nodiscard]] void *allocate_aligned(std::size_t size, std::size_t alignment) noexcept
{
    void *address = nullptr;
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    if ((alignment & (alignment - 1)) != 0)
        return nullptr;
    size = size == 0 ? 1 : size;
    if (posix_memalign(&address, alignment, size) == 0)
        return address;
    return allocate_fallback(size, alignment);
}

void release(void *address) noexcept
{
    if (address != nullptr && !release_mapped(address) && !release_direct(address))
        free(address);
}

[[noreturn]] void allocation_failure() noexcept
{
    __builtin_trap();
}
} // namespace

extern "C" __attribute__((visibility("hidden"))) int
__wrap_posix_memalign(void **address, std::size_t alignment, std::size_t size)
{
    const int result = __real_posix_memalign(address, alignment, size);
    if (result != 12)
        return result;
    size = size == 0 ? 1 : size;
    *address = allocate_fallback(size, alignment);
    return *address == nullptr ? 12 : 0;
}

#ifdef PS5_WRAP_MALLOC
extern "C" __attribute__((visibility("hidden"))) void *__wrap_malloc(std::size_t size)
{
    size = size == 0 ? 1 : size;
    if (void *address = __real_malloc(size))
        return address;
    return allocate_fallback(size, alignof(std::max_align_t));
}

extern "C" __attribute__((visibility("hidden"))) void *__wrap_calloc(std::size_t count,
                                                                     std::size_t size)
{
    if (void *address = __real_calloc(count, size))
        return address;
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count)
        return nullptr;
    const std::size_t bytes = count * size;
    const std::size_t allocation_size = bytes == 0 ? 1 : bytes;
    void *address = allocate_fallback(allocation_size, alignof(std::max_align_t));
    if (address != nullptr)
        std::memset(address, 0, bytes == 0 ? 1 : bytes);
    return address;
}
#endif

extern "C" __attribute__((visibility("hidden"))) void __wrap_free(void *address)
{
    if (address != nullptr && (release_mapped(address) || release_direct(address)))
        return;
    __real_free(address);
}

extern "C" __attribute__((noinline, visibility("hidden"))) bool
ps5ObserveOwnedAllocation(const void *address) noexcept
{
    __asm__ volatile("" : : "r"(address) : "memory");
    return address != nullptr;
}

extern "C" __attribute__((visibility("hidden"))) void ps5SetDirectFallback(int enabled) noexcept
{
    direct_fallback_enabled.store(enabled != 0, std::memory_order_release);
}

extern "C" __attribute__((visibility("hidden"))) bool
ps5SdIsDirectArenaRange(const void *address, std::size_t size) noexcept
{
    if (direct_arena == nullptr)
        return false;
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    const auto begin = reinterpret_cast<std::uintptr_t>(direct_arena);
    return value >= begin && value <= begin + direct_arena_size &&
           size <= begin + direct_arena_size - value;
}

extern "C" __attribute__((visibility("hidden"))) bool ps5SdReleaseDirectArenaIfEmpty() noexcept
{
    DirectArenaLock lock;
    if (direct_arena == nullptr)
        return true;
    if (direct_arena_used != 0 || direct_tail != nullptr)
        return false;
    const int unmap_result = munmap(direct_arena, direct_arena_size);
    const int release_result =
        sceKernelReleaseDirectMemory(direct_arena_physical, direct_arena_size);
    direct_arena = nullptr;
    direct_arena_physical = -1;
    return unmap_result == 0 && release_result == 0;
}

void *operator new(std::size_t size)
{
    if (void *address = allocate(size))
        return address;
    allocation_failure();
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return allocate(size);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return allocate(size);
}

void *operator new(std::size_t size, std::align_val_t alignment)
{
    if (void *address = allocate_aligned(size, static_cast<std::size_t>(alignment)))
        return address;
    allocation_failure();
}

void *operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void *address) noexcept
{
    release(address);
}
void operator delete[](void *address) noexcept
{
    release(address);
}
void operator delete(void *address, std::size_t) noexcept
{
    release(address);
}
void operator delete[](void *address, std::size_t) noexcept
{
    release(address);
}
void operator delete(void *address, std::align_val_t) noexcept
{
    release(address);
}
void operator delete[](void *address, std::align_val_t) noexcept
{
    release(address);
}
void operator delete(void *address, std::size_t, std::align_val_t) noexcept
{
    release(address);
}
void operator delete[](void *address, std::size_t, std::align_val_t) noexcept
{
    release(address);
}
void operator delete(void *address, const std::nothrow_t &) noexcept
{
    release(address);
}
void operator delete[](void *address, const std::nothrow_t &) noexcept
{
    release(address);
}
void operator delete(void *address, std::align_val_t, const std::nothrow_t &) noexcept
{
    release(address);
}
void operator delete[](void *address, std::align_val_t, const std::nothrow_t &) noexcept
{
    release(address);
}
