#!/usr/bin/env python3
"""Extract the 24 clockwise fruit-machine track cells into RGB565 tiles."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageFilter


CELL = 19
REFERENCE_SIZE = 1024

# Source order is converted to firmware order: top left-to-right, right
# top-to-bottom, bottom right-to-left, then left bottom-to-top.
SOURCE_BOXES = [
    (10, 10, 155, 150),
    # Give the crown extra source-space on every side. Its artwork reaches the
    # original cell's top and right edge, which otherwise loses a pixel after
    # 19x19 downsampling.
    (145, 5, 305, 150),
    (295, 10, 437, 150),
    (437, 10, 581, 150),
    (581, 10, 720, 150),
    (720, 10, 862, 150),
    (862, 10, 1010, 150),
    (862, 150, 1010, 282),
    (862, 282, 1010, 418),
    (862, 418, 1010, 554),
    (862, 554, 1010, 690),
    (862, 690, 1010, 825),
    (862, 825, 1010, 990),
    (720, 825, 862, 990),
    (581, 825, 720, 990),
    (437, 825, 581, 990),
    (295, 825, 437, 990),
    (155, 825, 295, 990),
    (10, 825, 155, 990),
    (10, 690, 155, 825),
    (10, 554, 155, 690),
    (10, 418, 155, 554),
    (10, 282, 155, 418),
    (10, 150, 155, 282),
]


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    if source.width != source.height:
        raise SystemExit(f"expected a square source, got {source.size}")
    scale = source.width / REFERENCE_SIZE

    tiles = []
    for box in SOURCE_BOXES:
        scaled_box = tuple(round(coordinate * scale) for coordinate in box)
        tile = source.crop(scaled_box).resize(
            (CELL, CELL), Image.Resampling.LANCZOS
        )
        tile = tile.filter(ImageFilter.UnsharpMask(radius=0.55, percent=155, threshold=2))
        tiles.append(tile)

    atlas = Image.new("RGB", (CELL * len(tiles), CELL))
    for index, tile in enumerate(tiles):
        atlas.paste(tile, (index * CELL, 0))

    asset_dir = args.project_root / "firmware" / "sticks3" / "assets"
    include_dir = args.project_root / "firmware" / "sticks3" / "include"
    source_dir = args.project_root / "firmware" / "sticks3" / "src"
    asset_dir.mkdir(parents=True, exist_ok=True)
    atlas.save(
        asset_dir / "fruit_track_tiles.png",
        format="PNG",
        optimize=True,
        compress_level=9,
    )

    header = """#pragma once

#include <stdint.h>

#define FRUIT_TRACK_IMAGE_COUNT 24
#define FRUIT_TRACK_IMAGE_CELL 19

extern const uint16_t fruit_track_image_rgb565[
    FRUIT_TRACK_IMAGE_COUNT * FRUIT_TRACK_IMAGE_CELL * FRUIT_TRACK_IMAGE_CELL];
"""
    (include_dir / "fruit_track_image.h").write_text(header, encoding="utf-8")

    # Firmware addresses each tile as one contiguous CELL*CELL block. Keep the
    # PNG as a horizontal contact sheet for inspection, but serialize the C
    # resource tile-by-tile rather than scanline-by-scanline across the atlas.
    values = []
    for tile in tiles:
        values.extend(rgb565(pixel) for pixel in tile.get_flattened_data())
    rows = []
    for offset in range(0, len(values), 12):
        rows.append(
            "    " + ", ".join(f"0x{value:04x}" for value in values[offset:offset + 12])
        )
    c_source = """#include "fruit_track_image.h"

const uint16_t fruit_track_image_rgb565[
    FRUIT_TRACK_IMAGE_COUNT * FRUIT_TRACK_IMAGE_CELL * FRUIT_TRACK_IMAGE_CELL] = {
""" + ",\n".join(rows) + "\n};\n"
    (source_dir / "fruit_track_image.c").write_text(c_source, encoding="utf-8")


if __name__ == "__main__":
    main()
