// ProsperoAI - Bakes the glyphs of a list of code points into a .huifont (host tool).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// From ProsperoTV (opengl-ui/tools/font-baker/bake_list.cpp), unchanged but for this note.
//
// usage: bake_list <font.ttf|otf> <out.huifont> <pixel_size> <sdf_range> <atlas_width> <ranges>
//
// The kit's own baker (tools/font-baker/bake_font.cpp, which this follows)
// knows two alphabets and a square atlas, and looks for kerning between every
// pair: right for a few hundred letters, not for the thousands of characters
// Chinese, Japanese and Korean text needs. This one takes the code points from
// a file (one "first-last" or "single" per line, hexadecimal, # comments),
// lets the atlas grow as tall as it has to, and writes no kerning.
//
// The distance field is not stb_truetype's own: that one measures to straight
// and quadratic edges only, and Noto Sans CJK is drawn with cubic curves
// (CFF outlines), whose strokes it would lose. Here the glyph is rasterised
// four times larger, which stb_truetype does for any outline, and the field
// is the exact distance to the edge of that picture.

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "gfx/font_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace
{

namespace ff = hui::gfx::font_format;

struct Baked
{
    int codepoint = 0;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    float advance = 0.0f;
    unsigned char *bitmap = nullptr;
    int x = 0;
    int y = 0;
};

template <typename T> void put(std::vector<unsigned char> &out, const T &value)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

constexpr int kSuper = 4; // the glyph is rasterised this many times larger

// Squared distance to the nearest zero of f along one line (Felzenszwalb and
// Huttenlocher's lower envelope of parabolas).
void distance_1d(const float *f, int n, float *d, int *v, float *z)
{
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; ++q)
    {
        float s;
        for (;;)
        {
            const int p = v[k];
            s = ((f[q] + static_cast<float>(q) * q) - (f[p] + static_cast<float>(p) * p)) /
                (2.0f * static_cast<float>(q - p));
            if (s > z[k])
                break;
            --k;
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; ++q)
    {
        while (z[k + 1] < static_cast<float>(q))
            ++k;
        const float dx = static_cast<float>(q - v[k]);
        d[q] = dx * dx + f[v[k]];
    }
}

// For every pixel, the distance to the nearest pixel that is inside (or, with
// inside false, outside) the glyph.
std::vector<float> distance_to(const std::vector<unsigned char> &mask, int w, int h, bool inside)
{
    const int longest = std::max(w, h);
    std::vector<float> field(static_cast<std::size_t>(w) * h);
    std::vector<float> f(longest), d(longest), z(longest + 1);
    std::vector<int> v(longest);
    for (std::size_t i = 0; i < field.size(); ++i)
        field[i] = (mask[i] != 0) == inside ? 0.0f : 1e20f;
    for (int x = 0; x < w; ++x)
    {
        for (int y = 0; y < h; ++y)
            f[y] = field[static_cast<std::size_t>(y) * w + x];
        distance_1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; ++y)
            field[static_cast<std::size_t>(y) * w + x] = d[y];
    }
    for (int y = 0; y < h; ++y)
    {
        distance_1d(&field[static_cast<std::size_t>(y) * w], w, d.data(), v.data(), z.data());
        for (int x = 0; x < w; ++x)
            field[static_cast<std::size_t>(y) * w + x] = std::sqrt(d[x]);
    }
    return field;
}

// The glyph's distance field at the baked size: 128 on its edge, falling by
// 128 / range per pixel outward. Returns nullptr for a glyph with no ink.
unsigned char *make_field(const stbtt_fontinfo &font, float scale, int codepoint, int range,
                          int *width, int *height, int *xoff, int *yoff)
{
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    const float large = scale * kSuper;
    stbtt_GetCodepointBitmapBox(&font, codepoint, large, large, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0)
        return nullptr;
    const int pad = range * kSuper;
    const auto down = [](int value) { return static_cast<int>(std::floor(static_cast<float>(value) / kSuper)); };
    const auto up = [](int value) { return static_cast<int>(std::ceil(static_cast<float>(value) / kSuper)); };
    const int left = down(x0 - pad);
    const int top = down(y0 - pad);
    const int w = up(x1 + pad) - left;
    const int h = up(y1 + pad) - top;
    const int large_w = w * kSuper;
    const int large_h = h * kSuper;
    std::vector<unsigned char> picture(static_cast<std::size_t>(large_w) * large_h, 0);
    stbtt_MakeCodepointBitmap(&font,
                              &picture[static_cast<std::size_t>(y0 - top * kSuper) * large_w +
                                       (x0 - left * kSuper)],
                              x1 - x0, y1 - y0, large_w, large, large, codepoint);
    for (unsigned char &value : picture)
        value = value >= 128 ? 1 : 0;
    const std::vector<float> to_inside = distance_to(picture, large_w, large_h, true);
    const std::vector<float> to_outside = distance_to(picture, large_w, large_h, false);

    auto *field = static_cast<unsigned char *>(std::malloc(static_cast<std::size_t>(w) * h));
    const float per_pixel = 128.0f / static_cast<float>(range);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            // The middle of the output pixel is the corner shared by four large ones.
            float signed_distance = 0.0f;
            for (int dy = -1; dy <= 0; ++dy)
            {
                for (int dx = -1; dx <= 0; ++dx)
                {
                    const std::size_t at =
                        static_cast<std::size_t>(y * kSuper + kSuper / 2 + dy) * large_w +
                        (x * kSuper + kSuper / 2 + dx);
                    signed_distance += picture[at] != 0 ? to_outside[at] - 0.5f : 0.5f - to_inside[at];
                }
            }
            signed_distance /= 4.0f * kSuper;
            const float value = 128.0f + signed_distance * per_pixel;
            field[static_cast<std::size_t>(y) * w + x] =
                static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
        }
    }
    *width = w;
    *height = h;
    *xoff = left;
    *yoff = top;
    return field;
}

bool read_ranges(const char *path, std::set<int> *codepoints)
{
    std::ifstream input(path);
    if (!input)
        return false;
    std::string line;
    while (std::getline(input, line))
    {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.resize(comment);
        char *end = nullptr;
        const long first = std::strtol(line.c_str(), &end, 16);
        if (end == line.c_str())
            continue;
        long last = first;
        if (*end == '-')
            last = std::strtol(end + 1, nullptr, 16);
        for (long c = first; c <= last && c <= 0x10ffff; ++c)
            codepoints->insert(static_cast<int>(c));
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr, "usage: %s font out.huifont pixel_size sdf_range atlas_width ranges.txt\n",
                     argv[0]);
        return 2;
    }
    const float pixel_size = std::strtof(argv[3], nullptr);
    const int range = std::atoi(argv[4]);
    const int atlas_width = std::atoi(argv[5]);
    std::set<int> wanted;
    if (!read_ranges(argv[6], &wanted) || wanted.empty())
    {
        std::fprintf(stderr, "cannot read code points from %s\n", argv[6]);
        return 1;
    }

    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),
                                    std::istreambuf_iterator<char>());
    stbtt_fontinfo font;
    if (data.empty() ||
        !stbtt_InitFont(&font, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)))
    {
        std::fprintf(stderr, "cannot read font %s\n", argv[1]);
        return 1;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixel_size);

    std::vector<Baked> glyphs;
    std::size_t missing = 0;
    for (const int codepoint : wanted)
    {
        if (codepoint != ' ' && stbtt_FindGlyphIndex(&font, codepoint) == 0)
        {
            ++missing;
            continue;
        }
        Baked glyph;
        glyph.codepoint = codepoint;
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &bearing);
        glyph.advance = static_cast<float>(advance) * scale;
        glyph.bitmap =
            make_field(font, scale, codepoint, range, &glyph.w, &glyph.h, &glyph.xoff, &glyph.yoff);
        glyphs.push_back(glyph);
    }

    // Shelf packing, tallest first, one pixel of spacing; as many shelves as it takes.
    std::vector<Baked *> order;
    for (Baked &glyph : glyphs)
        order.push_back(&glyph);
    std::stable_sort(order.begin(), order.end(),
                     [](const Baked *a, const Baked *b) { return a->h > b->h; });
    int pen_x = 1;
    int pen_y = 1;
    int shelf = 0;
    for (Baked *glyph : order)
    {
        if (glyph->bitmap == nullptr)
            continue;
        if (glyph->w + 2 > atlas_width)
        {
            std::fprintf(stderr, "atlas width %d is too small\n", atlas_width);
            return 1;
        }
        if (pen_x + glyph->w + 1 > atlas_width)
        {
            pen_x = 1;
            pen_y += shelf + 1;
            shelf = 0;
        }
        glyph->x = pen_x;
        glyph->y = pen_y;
        pen_x += glyph->w + 1;
        shelf = std::max(shelf, glyph->h);
    }
    const int atlas_height = pen_y + shelf + 1;
    if (atlas_height > 16384)
    {
        std::fprintf(stderr, "the atlas would be %d rows: more than a texture holds\n", atlas_height);
        return 1;
    }
    std::vector<unsigned char> atlas(static_cast<std::size_t>(atlas_width) * atlas_height, 0);
    for (const Baked &glyph : glyphs)
        for (int row = 0; glyph.bitmap != nullptr && row < glyph.h; ++row)
            std::memcpy(&atlas[static_cast<std::size_t>(glyph.y + row) * atlas_width + glyph.x],
                        glyph.bitmap + row * glyph.w, static_cast<std::size_t>(glyph.w));

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    ff::Header header{};
    header.magic = ff::kMagic;
    header.version = ff::kVersion;
    header.atlas_width = static_cast<std::uint16_t>(atlas_width);
    header.atlas_height = static_cast<std::uint16_t>(atlas_height);
    header.pixel_size = pixel_size;
    header.sdf_range = static_cast<float>(range);
    header.ascent = static_cast<float>(ascent) * scale;
    header.descent = static_cast<float>(descent) * scale;
    header.line_gap = static_cast<float>(line_gap) * scale;
    header.glyph_count = static_cast<std::uint32_t>(glyphs.size());
    header.kern_count = 0;

    std::vector<unsigned char> out;
    put(out, header);
    for (const Baked &glyph : glyphs) // already in code point order
    {
        ff::Glyph record{};
        record.codepoint = static_cast<std::uint32_t>(glyph.codepoint);
        record.x = static_cast<std::uint16_t>(glyph.x);
        record.y = static_cast<std::uint16_t>(glyph.y);
        record.w = static_cast<std::uint16_t>(glyph.w);
        record.h = static_cast<std::uint16_t>(glyph.h);
        record.offset_x = static_cast<float>(glyph.xoff);
        record.offset_y = static_cast<float>(glyph.yoff);
        record.advance = glyph.advance;
        put(out, record);
    }
    out.insert(out.end(), atlas.begin(), atlas.end());

    std::FILE *file = std::fopen(argv[2], "wb");
    if (file == nullptr || std::fwrite(out.data(), 1, out.size(), file) != out.size())
    {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(file);
    for (Baked &glyph : glyphs)
        std::free(glyph.bitmap);
    std::printf("%s: %zu glyphs (%zu asked for are not in the font), atlas %dx%d, %zu bytes\n",
                argv[2], glyphs.size(), missing, atlas_width, atlas_height, out.size());
    return 0;
}
