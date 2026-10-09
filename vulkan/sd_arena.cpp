// ProsperoAI - Memory the GPU can read, for the image runtime's large blocks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The image runtime computes on the CPU and on the GPU in turns, and hands the
// GPU a tensor where it lies when the GPU can read that memory. Every allocation
// of this build comes from the platform heap, which only the CPU can read, so
// each tensor would be copied through the 1 GiB scratch area, and the image
// decoder's last layers need more than that area can ever hold beside the
// Vulkan driver's own mappings.
//
// tools/prepare-hybrid-media.py therefore sends the image libraries' malloc
// family here. Blocks of 2 MiB and more are placed in one region mapped for the
// CPU and the GPU, as the AGC build's allocator does; smaller ones, and
// everything once the region is full or refused, go to the heap as before. The
// region is mapped at the first large block and given back when the image
// runtime has freed all of them.
#ifdef PROSPERO_HYBRID_MEDIA
#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

extern "C"
{
    std::int32_t sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t,
                                               int, std::int64_t *);
    std::int32_t sceKernelMapDirectMemory(void **, std::size_t, int, int, std::int64_t,
                                          std::size_t);
    std::int32_t sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
    std::int64_t sceKernelGetDirectMemorySize(void);
    int sceKernelDebugOutText(int, const char *);
    int munmap(void *, std::size_t);
}

namespace
{
constexpr std::size_t kUnit = 0x4000;         // direct memory's mapping unit, and every block's
constexpr std::size_t kSmallest = 2ULL << 20; // smaller blocks stay in the heap
// The AGC build reserves 4 GiB; this one shares direct memory with the Vulkan
// driver and the heap, so it asks for less and settles for what it is given.
constexpr std::size_t kSizes[] = {3ULL << 30, 2ULL << 30, 3ULL << 29};
constexpr int kMemoryType = 12;   // as the model runtimes' other direct memory
constexpr int kProtection = 0x33; // CPU and GPU, read and write

struct Block
{
    std::size_t offset, bytes;
    bool used;
};

std::mutex mutex;
std::uint8_t *region;
std::size_t region_bytes, used_bytes, peak_bytes;
std::int64_t physical = -1;
std::vector<Block> blocks; // in address order, covering the region
bool refused;              // not asked again until the image runtime is released

void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
void say(const char *format, ...)
{
    char line[192];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    sceKernelDebugOutText(0, line);
}

bool map_region()
{
    const std::int64_t total = sceKernelGetDirectMemorySize();
    for (const std::size_t bytes : kSizes)
    {
        std::int64_t start = -1;
        if (total <= 0 ||
            sceKernelAllocateDirectMemory(0, total, bytes, kUnit, kMemoryType, &start) != 0)
            continue;
        void *address = nullptr;
        if (sceKernelMapDirectMemory(&address, bytes, kProtection, 0, start, kUnit) != 0 ||
            address == nullptr)
        {
            sceKernelReleaseDirectMemory(start, bytes);
            continue;
        }
        region = static_cast<std::uint8_t *>(address);
        region_bytes = bytes;
        physical = start;
        used_bytes = peak_bytes = 0;
        blocks.assign(1, Block{0, bytes, false});
        say("[prosperoai] image memory: %zu MiB at %p\n", bytes >> 20, address);
        return true;
    }
    say("[prosperoai] image memory: none could be mapped, tensors will be copied\n");
    return false;
}

bool inside(const void *address)
{
    const auto *at = static_cast<const std::uint8_t *>(address);
    return region != nullptr && at >= region && at < region + region_bytes;
}

// The index of the block that starts at `address`, or the count.
std::size_t find(const void *address)
{
    const auto offset =
        static_cast<std::size_t>(static_cast<const std::uint8_t *>(address) - region);
    const auto at = std::lower_bound(blocks.begin(), blocks.end(), offset,
                                     [](const Block &block, std::size_t value)
                                     { return block.offset < value; });
    return at != blocks.end() && at->offset == offset && at->used
               ? static_cast<std::size_t>(at - blocks.begin())
               : blocks.size();
}

void *take(std::size_t bytes, std::size_t alignment)
{
    if (bytes < kSmallest || alignment > kUnit ||
        bytes > std::numeric_limits<std::size_t>::max() - kUnit)
        return nullptr;
    const std::lock_guard<std::mutex> lock(mutex);
    if (region == nullptr)
    {
        if (refused)
            return nullptr;
        if (!map_region())
        {
            refused = true;
            return nullptr;
        }
    }
    const std::size_t wanted = (bytes + kUnit - 1) & ~(kUnit - 1);
    for (std::size_t index = 0; index < blocks.size(); ++index)
    {
        if (blocks[index].used || blocks[index].bytes < wanted)
            continue;
        const std::size_t rest = blocks[index].bytes - wanted;
        const std::size_t offset = blocks[index].offset;
        blocks[index].bytes = wanted;
        blocks[index].used = true;
        if (rest != 0)
            blocks.insert(blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                          Block{offset + wanted, rest, false});
        used_bytes += wanted;
        peak_bytes = std::max(peak_bytes, used_bytes);
        return region + offset;
    }
    return nullptr;
}

// Frees a block of the region. False when `address` is not one.
bool give_back(void *address)
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (!inside(address))
        return false;
    std::size_t index = find(address);
    if (index == blocks.size())
        return true; // inside, not a block's start: nothing of the heap's to free either
    blocks[index].used = false;
    used_bytes -= blocks[index].bytes;
    if (index + 1 < blocks.size() && !blocks[index + 1].used)
    {
        blocks[index].bytes += blocks[index + 1].bytes;
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    }
    if (index > 0 && !blocks[index - 1].used)
    {
        blocks[index - 1].bytes += blocks[index].bytes;
        blocks.erase(blocks.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return true;
}

// The size of the region's block at `address`, or 0 when it is the heap's.
std::size_t block_bytes(const void *address)
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (!inside(address))
        return 0;
    const std::size_t index = find(address);
    return index == blocks.size() ? 0 : blocks[index].bytes;
}
} // namespace

extern "C" void *sd_private_malloc(std::size_t bytes)
{
    if (void *address = take(bytes, alignof(std::max_align_t)))
        return address;
    return std::malloc(bytes);
}

extern "C" void sd_private_free(void *address)
{
    if (address != nullptr && !give_back(address))
        std::free(address);
}

extern "C" void *sd_private_calloc(std::size_t count, std::size_t bytes)
{
    if (count != 0 && bytes > std::numeric_limits<std::size_t>::max() / count)
        return nullptr;
    if (void *address = take(count * bytes, alignof(std::max_align_t)))
        return std::memset(address, 0, count * bytes); // a block may have been used before
    return std::calloc(count, bytes);
}

extern "C" void *sd_private_realloc(void *address, std::size_t bytes)
{
    if (address == nullptr)
        return sd_private_malloc(bytes);
    const std::size_t held = block_bytes(address);
    if (held == 0)
        return std::realloc(address, bytes); // the heap's: it stays the heap's
    if (bytes == 0)
    {
        give_back(address);
        return nullptr;
    }
    if (bytes <= held && bytes >= kSmallest)
        return address;
    void *moved = sd_private_malloc(bytes);
    if (moved == nullptr)
        return nullptr; // the caller keeps its block
    std::memcpy(moved, address, std::min(held, bytes));
    give_back(address);
    return moved;
}

extern "C" int sd_private_posix_memalign(void **address, std::size_t alignment, std::size_t bytes)
{
    if (void *taken = take(bytes, alignment))
    {
        *address = taken;
        return 0;
    }
    return posix_memalign(address, alignment, bytes);
}

// The AGC backend asks this before it hands a tensor to the GPU where it lies.
extern "C" bool ps5SdIsDirectArenaRange(const void *address, std::size_t bytes)
{
    const std::lock_guard<std::mutex> lock(mutex);
    const auto *at = static_cast<const std::uint8_t *>(address);
    return inside(address) && bytes <= static_cast<std::size_t>(region + region_bytes - at);
}

// Called when the image runtime has let go of its model. False while a block is in use.
extern "C" bool ps5SdReleaseDirectArenaIfEmpty()
{
    const std::lock_guard<std::mutex> lock(mutex);
    refused = false;
    if (region == nullptr)
        return true;
    if (used_bytes != 0)
        return false;
    say("[prosperoai] image memory: released, %zu MiB were used at most\n", peak_bytes >> 20);
    const int unmapped = munmap(region, region_bytes);
    const int released = sceKernelReleaseDirectMemory(physical, region_bytes);
    region = nullptr;
    region_bytes = 0;
    physical = -1;
    blocks.clear();
    return unmapped == 0 && released == 0;
}
#endif
