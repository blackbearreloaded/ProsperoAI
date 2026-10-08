#!/usr/bin/env python3
"""Preserve the bundled multilingual BMFont as a coverage .huifont atlas."""
import argparse
import struct
from pathlib import Path
import xml.etree.ElementTree as ET


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


def convert(source, destination):
    font = ET.parse(source).getroot()
    common = font.find("common").attrib
    width, height = int(common["scaleW"]), int(common["scaleH"])
    baseline = int(common["base"])
    pages = sorted(font.find("pages"), key=lambda page: int(page.attrib["id"]))
    atlas = bytearray()
    for index, page in enumerate(pages):
        if int(page.attrib["id"]) != index:
            raise ValueError("non-contiguous BMFont pages")
        path = source.parent / page.attrib["file"]
        # Existing XML describes the original TGA; the packaged lossless file is RTA.
        if not path.exists():
            path = path.with_suffix(".rta")
        w, h, pixels = decode_rta(path.read_bytes())
        if (w, h) != (width, height):
            raise ValueError("BMFont page dimensions differ")
        atlas.extend(pixels)
    if not pages or width > 65535 or height * len(pages) > 65535:
        raise ValueError("atlas cannot fit huifont format")
    glyphs = sorted(font.find("chars"), key=lambda node: int(node.attrib["id"]))
    kerns = sorted(font.findall("kernings/kerning"),
                   key=lambda node: (int(node.attrib["first"]), int(node.attrib["second"])))
    size = abs(int(font.find("info").attrib["size"]))
    data = bytearray(struct.pack("<IIHHfffffII", 0x46505A50, 1, width,
                                height * len(pages), size, 0, baseline,
                                baseline - int(common["lineHeight"]), 0,
                                len(glyphs), len(kerns)))
    for node in glyphs:
        g = {key: int(value) for key, value in node.attrib.items()}
        if not 0 <= g["page"] < len(pages) or g["x"] + g["width"] > width or g["y"] + g["height"] > height:
            raise ValueError("glyph outside source page")
        data.extend(struct.pack("<IHHHHfff", g["id"], g["x"], g["y"] + g["page"] * height,
                                g["width"], g["height"], g["xoffset"],
                                g["yoffset"] - baseline, g["xadvance"]))
    for node in kerns:
        k = node.attrib
        data.extend(struct.pack("<IIf", int(k["first"]), int(k["second"]), float(k["amount"])))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data + atlas)
    print(f"{len(glyphs)} glyphs, {width}x{height * len(pages)}: {destination}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    convert(args.source, args.destination)
