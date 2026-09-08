// ProsperoGPT - Native PlayStation 5 local AI chat application.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL2/SDL.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/SystemInterface.h>

#include "bitmap_font_engine.hpp"
#include "gpt_app.hpp"
#include "gpt_ime.hpp"
#include "gpt_input.hpp"

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <pthread.h>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int ps5_agc_backend_reserve(void);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void *mmap(void *address, std::size_t length, int protection, int flags, int descriptor,
                      long offset);
extern "C" int munmap(void *address, std::size_t length);
extern "C" void *__dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

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

extern "C" void __assert(const char *, const char *, int, const char *)
{
    std::abort();
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

namespace
{

class AppSystemInterface final : public Rml::SystemInterface
{
  public:
    void JoinPath(Rml::String &result, const Rml::String &document,
                  const Rml::String &path) override
    {
        if (path.rfind("/download0/", 0) == 0)
            result = path;
        else
            Rml::SystemInterface::JoinPath(result, document, path);
    }
    double GetElapsedTime() override
    {
        return static_cast<double>(SDL_GetTicks64() - start_ticks_) / 1000.0;
    }

  private:
    Uint64 start_ticks_ = SDL_GetTicks64();
};

class AppFileInterface final : public Rml::FileInterface
{
  public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (!file)
        {
            const Rml::String app_path = "/app0/" + path;
            file = std::fopen(app_path.c_str(), "rb");
        }
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle file) override
    {
        if (file)
            std::fclose(reinterpret_cast<std::FILE *>(file));
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        return std::fread(buffer, 1, size, reinterpret_cast<std::FILE *>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        return fseeko(reinterpret_cast<std::FILE *>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        return static_cast<size_t>(ftello(reinterpret_cast<std::FILE *>(file)));
    }
};

class SdlRenderInterface final : public Rml::RenderInterfaceCompatibility
{
  public:
    SdlRenderInterface(SDL_Renderer *renderer, SDL_Surface *surface)
        : renderer_(renderer), surface_(surface)
    {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    }

    void RenderGeometry(Rml::Vertex *rml_vertices, int num_vertices, int *indices, int num_indices,
                        Rml::TextureHandle texture, const Rml::Vector2f &translation) override
    {
        AppTexture *app_texture = reinterpret_cast<AppTexture *>(texture);
        if (app_texture && RenderPixelAlignedQuads(rml_vertices, num_vertices, indices, num_indices,
                                                   app_texture, translation))
        {
            return;
        }

        std::vector<SDL_Vertex> vertices;
        vertices.reserve(static_cast<size_t>(num_vertices));
        for (int i = 0; i < num_vertices; ++i)
        {
            const Rml::Vertex &vertex = rml_vertices[i];
            SDL_Vertex sdl_vertex{};
            sdl_vertex.position = {vertex.position.x + translation.x,
                                   vertex.position.y + translation.y};
            sdl_vertex.color = {vertex.colour.red, vertex.colour.green, vertex.colour.blue,
                                vertex.colour.alpha};
            sdl_vertex.tex_coord = {vertex.tex_coord.x, vertex.tex_coord.y};
            vertices.push_back(sdl_vertex);
        }
        SDL_RenderGeometry(renderer_, app_texture ? app_texture->texture : nullptr, vertices.data(),
                           num_vertices, indices, num_indices);
    }

    bool LoadTexture(Rml::TextureHandle &texture_handle, Rml::Vector2i &texture_dimensions,
                     const Rml::String &source) override
    {
        texture_handle = {};
        texture_dimensions = {};

        std::FILE *file = std::fopen(source.c_str(), "rb");
        if (!file && !source.empty() && source[0] != '/')
        {
            const Rml::String app_path = "/app0/" + source;
            file = std::fopen(app_path.c_str(), "rb");
        }
        if (!file)
            return false;

        unsigned char header[18]{};
        const bool header_read = std::fread(header, 1, sizeof(header), file) == sizeof(header);
        int width = 0;
        int height = 0;
        SdlBuffer pixels;
        std::size_t pixel_bytes = 0;
        bool decoded = false;

        if (header_read && std::memcmp(header, "RTA1", 4) == 0)
        {
            width = ReadLe16(header + 4);
            height = ReadLe16(header + 6);
            const std::uint32_t pixel_count = ReadLe32(header + 8);
            const std::uint32_t payload_length = ReadLe32(header + 12);
            const std::size_t expected_pixels =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            if (width > 0 && height > 0 && pixel_count == expected_pixels &&
                expected_pixels <= std::numeric_limits<std::size_t>::max() / 4 &&
                payload_length <= expected_pixels * 2 && fseeko(file, 16, SEEK_SET) == 0)
            {
                SdlBuffer payload(static_cast<unsigned char *>(SDL_malloc(payload_length)));
                pixels.reset(static_cast<unsigned char *>(SDL_malloc(expected_pixels * 4)));
                pixel_bytes = expected_pixels * 4;
                if (payload && pixels &&
                    std::fread(payload.get(), 1, payload_length, file) == payload_length)
                {
                    decoded = DecodeRadioAtlas(payload.get(), payload_length, expected_pixels,
                                               pixels.get());
                }
            }
        }
        else
        {
            width = header[12] | (header[13] << 8);
            height = header[14] | (header[15] << 8);
            const bool supported = header_read && header[0] == 0 && header[1] == 0 &&
                                   header[2] == 2 && width > 0 && height > 0 && header[16] == 32 &&
                                   (header[17] & 0x0f) == 8 && (header[17] & 0x30) == 0x20;
            if (supported &&
                static_cast<std::size_t>(width) <= std::numeric_limits<std::size_t>::max() /
                                                       (static_cast<std::size_t>(height) * 4))
            {
                pixel_bytes =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
                pixels.reset(static_cast<unsigned char *>(SDL_malloc(pixel_bytes)));
                decoded = pixels && std::fread(pixels.get(), 1, pixel_bytes, file) == pixel_bytes;
            }
        }
        std::fclose(file);
        if (!decoded)
            return false;

        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            pixels.get(), width, height, 32, width * 4, SDL_PIXELFORMAT_BGRA32);
        if (!surface)
            return false;

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (!texture)
            return false;

        if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0 ||
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest) != 0)
        {
            SDL_DestroyTexture(texture);
            return false;
        }

        auto *app_texture = new (std::nothrow) AppTexture;
        if (!app_texture)
        {
            SDL_DestroyTexture(texture);
            return false;
        }
        app_texture->texture = texture;
        app_texture->width = width;
        app_texture->height = height;
        app_texture->exact_pixels = source.find("lvgl-bitmap") != Rml::String::npos;
        if (app_texture->exact_pixels)
        {
            for (std::size_t i = 0; i < pixel_bytes; i += 4)
                std::swap(pixels.get()[i], pixels.get()[i + 2]);
            app_texture->rgba = std::move(pixels);
            app_texture->surface = SDL_CreateRGBSurfaceWithFormatFrom(
                app_texture->rgba.get(), width, height, 32, width * 4, SDL_PIXELFORMAT_RGBA32);
            if (!app_texture->surface ||
                SDL_SetSurfaceBlendMode(app_texture->surface, SDL_BLENDMODE_BLEND) != 0)
            {
                SDL_FreeSurface(app_texture->surface);
                SDL_DestroyTexture(texture);
                delete app_texture;
                return false;
            }
        }

        texture_dimensions = {width, height};
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    bool GenerateTexture(Rml::TextureHandle &texture_handle, const Rml::byte *source,
                         const Rml::Vector2i &dimensions) override
    {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            const_cast<Rml::byte *>(source), dimensions.x, dimensions.y, 32, dimensions.x * 4,
            SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return false;

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer_, surface);
        SDL_FreeSurface(surface);
        if (texture)
        {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
        }
        if (!texture)
            return false;

        auto *app_texture = new (std::nothrow) AppTexture;
        if (!app_texture)
        {
            SDL_DestroyTexture(texture);
            return false;
        }
        app_texture->texture = texture;
        app_texture->width = dimensions.x;
        app_texture->height = dimensions.y;
        texture_handle = reinterpret_cast<Rml::TextureHandle>(app_texture);
        return true;
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        auto *app_texture = reinterpret_cast<AppTexture *>(texture);
        if (!app_texture)
            return;
        SDL_FreeSurface(app_texture->surface);
        SDL_DestroyTexture(app_texture->texture);
        delete app_texture;
    }

    void EnableScissorRegion(bool enable) override
    {
        scissor_enabled_ = enable;
        SDL_RenderSetClipRect(renderer_, enable ? &scissor_ : nullptr);
    }

    void SetScissorRegion(int x, int y, int width, int height) override
    {
        scissor_ = {x, y, width, height};
        if (scissor_enabled_)
            SDL_RenderSetClipRect(renderer_, &scissor_);
    }

  private:
    struct SdlFree
    {
        void operator()(void *allocation) const noexcept
        {
            SDL_free(allocation);
        }
    };

    using SdlBuffer = std::unique_ptr<unsigned char, SdlFree>;

    struct AppTexture
    {
        SDL_Texture *texture = nullptr;
        SDL_Surface *surface = nullptr;
        int width = 0;
        int height = 0;
        bool exact_pixels = false;
        SdlBuffer rgba;
    };

    static std::uint16_t ReadLe16(const unsigned char *value)
    {
        return static_cast<std::uint16_t>(value[0]) | (static_cast<std::uint16_t>(value[1]) << 8);
    }

    static std::uint32_t ReadLe32(const unsigned char *value)
    {
        return static_cast<std::uint32_t>(value[0]) | (static_cast<std::uint32_t>(value[1]) << 8) |
               (static_cast<std::uint32_t>(value[2]) << 16) |
               (static_cast<std::uint32_t>(value[3]) << 24);
    }

    static bool DecodeRadioAtlas(const unsigned char *payload, std::size_t payload_size,
                                 std::size_t pixel_count, unsigned char *pixels)
    {
        std::memset(pixels, 255, pixel_count * 4);
        std::size_t input = 0;
        std::size_t output = 0;
        while (input < payload_size && output < pixel_count)
        {
            const unsigned char token = payload[input++];
            const std::size_t length = static_cast<std::size_t>(token & 0x7f) + 1;
            if (length > pixel_count - output)
                return false;
            if ((token & 0x80) == 0)
            {
                for (std::size_t index = 0; index < length; ++index)
                    pixels[(output + index) * 4 + 3] = 0;
            }
            else
            {
                const std::size_t bytes = (length + 1) / 2;
                if (bytes > payload_size - input)
                    return false;
                for (std::size_t index = 0; index < length; ++index)
                {
                    const unsigned char packed = payload[input + index / 2];
                    const unsigned char alpha =
                        (index & 1) != 0 ? static_cast<unsigned char>((packed & 0x0f) * 17)
                                         : static_cast<unsigned char>((packed >> 4) * 17);
                    pixels[(output + index) * 4 + 3] = alpha;
                }
                input += bytes;
            }
            output += length;
        }
        return input == payload_size && output == pixel_count;
    }

    struct PixelCopy
    {
        SDL_Rect source;
        SDL_Rect destination;
        Rml::ColourbPremultiplied colour;
    };

    static int RoundPixel(float value)
    {
        return static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
    }

    static bool SameColor(const Rml::Vertex &lhs, const Rml::Vertex &rhs)
    {
        return lhs.colour.red == rhs.colour.red && lhs.colour.green == rhs.colour.green &&
               lhs.colour.blue == rhs.colour.blue && lhs.colour.alpha == rhs.colour.alpha;
    }

    bool RenderPixelAlignedQuads(Rml::Vertex *vertices, int num_vertices, int *indices,
                                 int num_indices, AppTexture *texture,
                                 const Rml::Vector2f &translation)
    {
        if (num_vertices <= 0 || num_indices <= 0 || num_indices % 6 != 0)
            return false;

        const int texture_width = texture->width;
        const int texture_height = texture->height;
        if (texture_width <= 0 || texture_height <= 0)
            return false;

        const int num_quads = num_indices / 6;
        std::vector<PixelCopy> copies;
        copies.reserve(static_cast<size_t>(num_quads));
        for (int quad = 0; quad < num_quads; ++quad)
        {
            const int index = quad * 6;
            const int i0 = indices[index + 0];
            const int i3 = indices[index + 1];
            const int i1 = indices[index + 2];
            const int i2 = indices[index + 5];
            if (i0 < 0 || i0 >= num_vertices || i1 < 0 || i1 >= num_vertices || i2 < 0 ||
                i2 >= num_vertices || i3 < 0 || i3 >= num_vertices || indices[index + 3] != i1 ||
                indices[index + 4] != i3 || i0 == i1 || i0 == i2 || i0 == i3 || i1 == i2 ||
                i1 == i3 || i2 == i3)
                return false;

            const Rml::Vertex &v0 = vertices[i0];
            const Rml::Vertex &v1 = vertices[i1];
            const Rml::Vertex &v2 = vertices[i2];
            const Rml::Vertex &v3 = vertices[i3];
            if (v0.position.y != v1.position.y || v1.position.x != v2.position.x ||
                v2.position.y != v3.position.y || v3.position.x != v0.position.x ||
                v0.tex_coord.y != v1.tex_coord.y || v1.tex_coord.x != v2.tex_coord.x ||
                v2.tex_coord.y != v3.tex_coord.y || v3.tex_coord.x != v0.tex_coord.x ||
                !SameColor(v0, v1) || !SameColor(v0, v2) || !SameColor(v0, v3))
                return false;

            const int source_width = RoundPixel((v1.tex_coord.x - v0.tex_coord.x) * texture_width);
            const int source_height =
                RoundPixel((v3.tex_coord.y - v0.tex_coord.y) * texture_height);
            const int destination_width = RoundPixel(v1.position.x - v0.position.x);
            const int destination_height = RoundPixel(v3.position.y - v0.position.y);
            if (source_width <= 0 || source_height <= 0 || source_width != destination_width ||
                source_height != destination_height)
                return false;

            SDL_Rect source{RoundPixel(v0.tex_coord.x * texture_width),
                            RoundPixel(v0.tex_coord.y * texture_height), source_width,
                            source_height};
            if (source.x < 0 || source.y < 0 || source.x + source.w > texture_width ||
                source.y + source.h > texture_height)
                return false;
            copies.push_back(
                {source,
                 {RoundPixel(v0.position.x + translation.x),
                  RoundPixel(v0.position.y + translation.y), destination_width, destination_height},
                 v0.colour});
        }

        if (texture->exact_pixels)
            return CompositeExactPixels(*texture, copies);

        for (const PixelCopy &copy : copies)
        {
            SDL_SetTextureColorMod(texture->texture, copy.colour.red, copy.colour.green,
                                   copy.colour.blue);
            SDL_SetTextureAlphaMod(texture->texture, copy.colour.alpha);
            SDL_RenderCopy(renderer_, texture->texture, &copy.source, &copy.destination);
        }

        SDL_SetTextureColorMod(texture->texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(texture->texture, 255);
        return true;
    }

    bool CompositeExactPixels(const AppTexture &texture, const std::vector<PixelCopy> &copies)
    {
        if (!surface_ || !texture.surface)
            return false;
        SDL_RenderFlush(renderer_);

        SDL_Rect old_clip{};
        SDL_GetClipRect(surface_, &old_clip);
        SDL_SetClipRect(surface_, scissor_enabled_ ? &scissor_ : nullptr);
        for (const PixelCopy &copy : copies)
        {
            SDL_SetSurfaceColorMod(texture.surface, copy.colour.red, copy.colour.green,
                                   copy.colour.blue);
            SDL_SetSurfaceAlphaMod(texture.surface, copy.colour.alpha);
            SDL_Rect destination = copy.destination;
            SDL_BlitSurface(texture.surface, &copy.source, surface_, &destination);
        }
        SDL_SetSurfaceColorMod(texture.surface, 255, 255, 255);
        SDL_SetSurfaceAlphaMod(texture.surface, 255);
        SDL_SetClipRect(surface_, &old_clip);
        return true;
    }

    SDL_Renderer *renderer_;
    SDL_Surface *surface_;
    SDL_Rect scissor_{};
    bool scissor_enabled_ = false;
};

bool LoadFonts()
{
    static constexpr const char *kBitmapFonts[] = {
        "assets/ui/fonts/lvgl-bitmap/Montserrat-20.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-24.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-28.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-32.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-36.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-40.fnt",
        "assets/ui/fonts/lvgl-bitmap/Montserrat-48.fnt",
    };
    for (const char *font : kBitmapFonts)
    {
        if (!Rml::LoadFontFace(font))
            return false;
    }
    static constexpr const char *kMultilingualFonts[] = {
        "assets/ui/fonts/lvgl-bitmap/multilingual/Radio-20.fnt",
        "assets/ui/fonts/lvgl-bitmap/multilingual/Radio-24.fnt",
        "assets/ui/fonts/lvgl-bitmap/multilingual/Radio-28.fnt",
        "assets/ui/fonts/lvgl-bitmap/multilingual/Radio-32.fnt",
    };
    for (const char *font : kMultilingualFonts)
    {
        if (!Rml::LoadFontFace(font))
            return false;
    }
    return true;
}

[[noreturn]] void KeepProcessAlive()
{
    for (;;)
        sceKernelUsleep(1000000);
}

void PresentColor(SDL_Renderer *renderer, SDL_Window *window, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

bool RunApp()
{
    if (ps5_agc_backend_reserve() != 0)
        return false;
    if (SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked) != 0)
    {
        return false;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        return false;
    }
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window *window = SDL_CreateWindow("ProsperoGPT", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer)
        return false;

    PresentColor(renderer, window, 239, 233, 220);

    AppSystemInterface system_interface;
    AppFileInterface file_interface;
    SdlRenderInterface render_interface(renderer, surface);
    BitmapFontEngine font_engine;
    Rml::RenderInterface *adapted_render_interface = render_interface.GetAdaptedInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetRenderInterface(adapted_render_interface);
    Rml::SetFontEngineInterface(&font_engine);

    bool running = Rml::Initialise();
    if (running)
        running = LoadFonts();
    Rml::Context *context =
        running ? Rml::CreateContext("prosperogpt", {1920, 1080}, adapted_render_interface)
                : nullptr;
    Rml::ElementDocument *document =
        context ? context->LoadDocument("assets/ui/main.rml") : nullptr;
    ProsperoGptApp app;
    bool input_ready = false;
    bool ime_ready = false;
    if (document)
    {
        document->Show();
        input_ready = gpt_input_init();
        ime_ready = input_ready && gpt_ime_init();
        running = input_ready && ime_ready && app.Initialize(document);
        if (running)
            sceSystemServiceHideSplashScreen();
    }
    else
    {
        PresentColor(renderer, window, 180, 20, 40);
        running = false;
    }

    while (running)
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
        }
        gpt_input_poll();
        gpt_input_event_t input{};
        while (gpt_input_next(&input))
            app.HandleInput(input);
        gpt_ime_poll();
        app.Poll();
        context->Update();
        SDL_SetRenderDrawColor(renderer, 239, 233, 220, 255);
        SDL_RenderClear(renderer);
        context->Render();
        SDL_RenderFlush(renderer);
        SDL_UpdateWindowSurface(window);
        sceKernelUsleep(16667);
    }

    app.Shutdown();
    if (ime_ready)
        gpt_ime_shutdown();
    if (input_ready)
        gpt_input_shutdown();
    if (document)
        document->Close();
    if (context)
        Rml::RemoveContext("prosperogpt");
    Rml::Shutdown();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return false;
}

} // namespace

int main()
{
    RunApp();
    KeepProcessAlive();
}
