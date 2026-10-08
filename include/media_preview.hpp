// Bounded worker-side decoding of generated TGA images.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace prospero
{
struct ImagePreview
{
    std::string path;
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
std::shared_ptr<const ImagePreview> load_image_preview(const std::string &path);
} // namespace prospero
