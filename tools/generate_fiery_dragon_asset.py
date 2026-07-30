#!/usr/bin/env python3
"""Create the 95x95 RGB565 center-board resource from a supplied image."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageFilter


WIDTH = 95
HEIGHT = 95
TRIM = 4


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    source = source.crop((TRIM, TRIM, source.width - TRIM, source.height - TRIM))
    fitted_height = round(source.height * WIDTH / source.width)
    fitted = source.resize((WIDTH, fitted_height), Image.Resampling.LANCZOS)
    fitted = fitted.filter(ImageFilter.UnsharpMask(radius=0.6, percent=135, threshold=2))

    output = Image.new("RGB", (WIDTH, HEIGHT), (16, 23, 42))
    y_offset = (HEIGHT - fitted_height) // 2
    output.paste(fitted, (0, y_offset))

    asset_dir = args.project_root / "firmware" / "sticks3" / "assets"
    include_dir = args.project_root / "firmware" / "sticks3" / "include"
    source_dir = args.project_root / "firmware" / "sticks3" / "src"
    asset_dir.mkdir(parents=True, exist_ok=True)
    output.save(
        asset_dir / "fiery_dragon_center.png",
        format="PNG",
        optimize=True,
        compress_level=9,
    )

    header = """#pragma once

#include <stdint.h>

#define FIERY_DRAGON_IMAGE_WIDTH 95
#define FIERY_DRAGON_IMAGE_HEIGHT 95

extern const uint16_t fiery_dragon_image_rgb565[
    FIERY_DRAGON_IMAGE_WIDTH * FIERY_DRAGON_IMAGE_HEIGHT];
"""
    (include_dir / "fiery_dragon_image.h").write_text(header, encoding="utf-8")

    values = [rgb565(pixel) for pixel in output.get_flattened_data()]
    rows = []
    for offset in range(0, len(values), 12):
        rows.append(
            "    " + ", ".join(f"0x{value:04x}" for value in values[offset:offset + 12])
        )
    c_source = """#include "fiery_dragon_image.h"

const uint16_t fiery_dragon_image_rgb565[
    FIERY_DRAGON_IMAGE_WIDTH * FIERY_DRAGON_IMAGE_HEIGHT] = {
""" + ",\n".join(rows) + "\n};\n"
    (source_dir / "fiery_dragon_image.c").write_text(c_source, encoding="utf-8")


if __name__ == "__main__":
    main()
