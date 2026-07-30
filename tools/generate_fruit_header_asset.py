#!/usr/bin/env python3
"""Create the 135x22 RGB565 retro BONUS WIN / CREDIT header."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


WIDTH = 135
HEIGHT = 22


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    fitted = source.resize((WIDTH, 18), Image.Resampling.LANCZOS)
    fitted = fitted.filter(ImageFilter.UnsharpMask(radius=0.5, percent=145, threshold=2))
    output = Image.new("RGB", (WIDTH, HEIGHT), (4, 3, 5))
    output.paste(fitted, (0, 2))
    # Remove every source digit after scaling. The firmware draws both the
    # inactive segments and live values, so the background must be truly blank.
    draw = ImageDraw.Draw(output)
    draw.rectangle((4, 8, 61, 17), fill=(5, 4, 5))
    draw.rectangle((74, 8, 131, 17), fill=(5, 4, 5))

    asset_dir = args.project_root / "firmware" / "sticks3" / "assets"
    include_dir = args.project_root / "firmware" / "sticks3" / "include"
    source_dir = args.project_root / "firmware" / "sticks3" / "src"
    asset_dir.mkdir(parents=True, exist_ok=True)
    output.save(
        asset_dir / "fruit_header.png",
        format="PNG",
        optimize=True,
        compress_level=9,
    )

    header = """#pragma once

#include <stdint.h>

#define FRUIT_HEADER_IMAGE_WIDTH 135
#define FRUIT_HEADER_IMAGE_HEIGHT 22

extern const uint16_t fruit_header_image_rgb565[
    FRUIT_HEADER_IMAGE_WIDTH * FRUIT_HEADER_IMAGE_HEIGHT];
"""
    (include_dir / "fruit_header_image.h").write_text(header, encoding="utf-8")

    values = [rgb565(pixel) for pixel in output.get_flattened_data()]
    rows = []
    for offset in range(0, len(values), 12):
        rows.append(
            "    " + ", ".join(f"0x{value:04x}" for value in values[offset:offset + 12])
        )
    c_source = """#include "fruit_header_image.h"

const uint16_t fruit_header_image_rgb565[
    FRUIT_HEADER_IMAGE_WIDTH * FRUIT_HEADER_IMAGE_HEIGHT] = {
""" + ",\n".join(rows) + "\n};\n"
    (source_dir / "fruit_header_image.c").write_text(c_source, encoding="utf-8")


if __name__ == "__main__":
    main()
