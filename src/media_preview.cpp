// SPDX-License-Identifier: GPL-3.0-or-later
#include "media_preview.hpp"
#include <algorithm>
#include <cstdio>
namespace prospero
{
std::shared_ptr<const ImagePreview> load_image_preview(const std::string &path)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (!file)
        return {};
    std::uint8_t header[18]{};
    if (std::fread(header, 1, sizeof(header), file) != sizeof(header))
    {
        std::fclose(file);
        return {};
    }
    const unsigned width = header[12] | (static_cast<unsigned>(header[13]) << 8);
    const unsigned height = header[14] | (static_cast<unsigned>(header[15]) << 8);
    const unsigned channels = header[16] / 8;
    if (header[1] != 0 || header[2] != 2 || (header[16] != 24 && header[16] != 32) || !width ||
        !height || width > 4096 || height > 4096 || (header[17] & 0xc0) ||
        std::fseek(file, header[0], SEEK_CUR) != 0)
    {
        std::fclose(file);
        return {};
    }
    // Preview memory is bounded even when an imported session references a large image.
    const unsigned divisor = std::max(1U, (std::max(width, height) + 1023) / 1024);
    auto image = std::make_shared<ImagePreview>();
    image->path = path;
    image->width = (width + divisor - 1) / divisor;
    image->height = (height + divisor - 1) / divisor;
    image->rgba.resize(static_cast<std::size_t>(image->width) * image->height * 4);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * channels);
    for (unsigned y = 0; y < height; ++y)
    {
        if (std::fread(row.data(), 1, row.size(), file) != row.size())
        {
            std::fclose(file);
            return {};
        }
        const unsigned source_y = (header[17] & 0x20) ? y : height - 1 - y;
        if (source_y % divisor)
            continue;
        for (unsigned x = 0; x < image->width; ++x)
        {
            const unsigned source_x = (header[17] & 0x10) ? width - 1 - x * divisor : x * divisor;
            const auto *pixel = row.data() + source_x * channels;
            auto *out = image->rgba.data() +
                        (static_cast<std::size_t>(source_y / divisor) * image->width + x) * 4;
            out[0] = pixel[2];
            out[1] = pixel[1];
            out[2] = pixel[0];
            out[3] = channels == 4 ? pixel[3] : 255;
        }
    }
    std::fclose(file);
    return image;
}
} // namespace prospero
