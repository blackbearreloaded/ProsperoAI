#!/usr/bin/env python3
"""Turn the first interface's multilingual BMFont into a small .huifont the kit draws.

usage: tools/convert-ui-font.py <font.fnt> <out.huifont> [--exclude <ranges.txt> ...]

The bitmap font is kept as a last-resort face for the scripts no other face of the
interface covers. Code points listed in the --exclude files (the lists the Noto Sans
faces are baked from, one "first-last" or "single" per line, hexadecimal) are left
out, and what remains is packed again into one atlas.

The atlas holds coverage (16 levels), not distances. Declared with a spread of half a
pixel, the kit's glyph shader reproduces that coverage at the atlas's own size and
sharpens it when the text is drawn larger, so no shader of its own is needed.
"""
import argparse
import struct
from pathlib import Path
import xml.etree.ElementTree as ET

ATLAS_WIDTH = 1024
PADDING = 1  # empty pixels around every glyph, so neighbours never bleed when filtered


def decode_rta(data):
    if len(data) < 16 or data[:4] != b"RTA1":
        raise ValueError("invalid RTA header")
    width, height, pixels, payload = struct.unpack_from("<HHII", data, 4)
    if not width or not height or pixels != width * height or payload != len(data) - 16:
        raise ValueError("invalid RTA dimensions or payload")
    result = bytearray()
    cursor = 16
    while cursor < len(data):
        token = data[cursor]
        cursor += 1
        count = (token & 127) + 1
        if len(result) + count > pixels:
            raise ValueError("RTA run exceeds atlas")
        if token & 128:
            packed = (count + 1) // 2
            if cursor + packed > len(data):
                raise ValueError("truncated RTA literal")
            for index in range(count):
                value = data[cursor + index // 2]
                result.append(((value >> 4) if index % 2 == 0 else (value & 15)) * 17)
            cursor += packed
        else:
            result.extend(bytes(count))
    if len(result) != pixels:
        raise ValueError("incomplete RTA atlas")
    return width, height, result


def read_ranges(paths):
    excluded = set()
    for path in paths:
        for line in path.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            first, _, last = line.partition("-")
            excluded.update(range(int(first, 16), int(last or first, 16) + 1))
    return excluded


def convert(source, destination, excluded):
    font = ET.parse(source).getroot()
    common = font.find("common").attrib
    width, height = int(common["scaleW"]), int(common["scaleH"])
    baseline = int(common["base"])
    pages = sorted(font.find("pages"), key=lambda page: int(page.attrib["id"]))
    sheets = []
    for index, page in enumerate(pages):
        if int(page.attrib["id"]) != index:
            raise ValueError("non-contiguous BMFont pages")
        path = source.parent / page.attrib["file"]
        # The XML names the original TGA; the file kept here is its lossless RTA.
        if not path.exists():
            path = path.with_suffix(".rta")
        w, h, pixels = decode_rta(path.read_bytes())
        if (w, h) != (width, height):
            raise ValueError("BMFont page dimensions differ")
        sheets.append(pixels)
    if not sheets:
        raise ValueError("BMFont has no pages")

    glyphs = []
    for node in font.find("chars"):
        g = {key: int(value) for key, value in node.attrib.items()}
        if g["id"] in excluded:
            continue
        if (not 0 <= g["page"] < len(sheets) or g["x"] + g["width"] > width
                or g["y"] + g["height"] > height):
            raise ValueError("glyph outside source page")
        glyphs.append(g)
    kept = {g["id"] for g in glyphs}

    # Shelves of glyphs of similar height, tallest first.
    placed = {}
    x = y = PADDING
    shelf = 0
    for g in sorted(glyphs, key=lambda g: (-g["height"], g["id"])):
        if g["width"] + 2 * PADDING > ATLAS_WIDTH:
            raise ValueError("glyph wider than the atlas")
        if x + g["width"] + PADDING > ATLAS_WIDTH:
            x = PADDING
            y += shelf + PADDING
            shelf = 0
        placed[g["id"]] = (x, y)
        x += g["width"] + PADDING
        shelf = max(shelf, g["height"])
    atlas_height = y + shelf + PADDING
    if atlas_height > 65535:
        raise ValueError("atlas cannot fit huifont format")
    atlas = bytearray(ATLAS_WIDTH * atlas_height)
    for g in glyphs:
        gx, gy = placed[g["id"]]
        sheet = sheets[g["page"]]
        for row in range(g["height"]):
            start = (g["y"] + row) * width + g["x"]
            target = (gy + row) * ATLAS_WIDTH + gx
            atlas[target:target + g["width"]] = sheet[start:start + g["width"]]

    kerns = sorted(
        (node for node in font.findall("kernings/kerning")
         if int(node.attrib["first"]) in kept and int(node.attrib["second"]) in kept),
        key=lambda node: (int(node.attrib["first"]), int(node.attrib["second"])))
    size = abs(int(font.find("info").attrib["size"]))
    data = bytearray(struct.pack("<IIHHfffffII", 0x46505A50, 1, ATLAS_WIDTH, atlas_height,
                                 size, 0.5, baseline, baseline - int(common["lineHeight"]), 0,
                                 len(glyphs), len(kerns)))
    for g in sorted(glyphs, key=lambda g: g["id"]):
        gx, gy = placed[g["id"]]
        data.extend(struct.pack("<IHHHHfff", g["id"], gx, gy, g["width"], g["height"],
                                g["xoffset"], g["yoffset"] - baseline, g["xadvance"]))
    for node in kerns:
        k = node.attrib
        data.extend(struct.pack("<IIf", int(k["first"]), int(k["second"]), float(k["amount"])))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data + atlas)
    print(f"{destination}: {len(glyphs)} glyphs, {len(kerns)} kerning pairs, "
          f"atlas {ATLAS_WIDTH}x{atlas_height}, {len(data) + len(atlas)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--exclude", type=Path, action="append", default=[])
    args = parser.parse_args()
    convert(args.source, args.destination, read_ranges(args.exclude))
