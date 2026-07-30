#!/usr/bin/env python3
"""Create the 135x40 RGB565 retro fruit-machine control console."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter


WIDTH = 135
HEIGHT = 40


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument(
        "--variant",
        choices=("normal", "pending"),
        default="normal",
        help="Generate the normal 2X/CLR bar or the pending-win -/+ bar.",
    )
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    if args.variant == "pending":
        # The promotional pending-win artwork is presented on white. Detect
        # the saturated cabinet bounds so none of that white matte is scaled
        # into the device image.
        white = Image.new("RGB", source.size, "white")
        difference = ImageChops.difference(source, white).convert("L")
        mask = difference.point(lambda value: 255 if value > 24 else 0)
        crop = mask.getbbox()
        if crop is None:
            raise ValueError("pending controls source has no non-white artwork")
    else:
        # Crop away the presentation margin while retaining the complete
        # red cabinet bezel, button shadows, and control-console background.
        crop = (
            0,
            round(source.height * 0.156),
            source.width,
            round(source.height * 0.720),
        )
    output = source.crop(crop).resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    output = output.filter(ImageFilter.UnsharpMask(radius=0.45, percent=150, threshold=2))

    asset_dir = args.project_root / "firmware" / "sticks3" / "assets"
    include_dir = args.project_root / "firmware" / "sticks3" / "include"
    source_dir = args.project_root / "firmware" / "sticks3" / "src"
    asset_dir.mkdir(parents=True, exist_ok=True)
    stem = "fruit_controls" if args.variant == "normal" else "fruit_controls_pending"
    macro = (
        "FRUIT_CONTROLS"
        if args.variant == "normal"
        else "FRUIT_CONTROLS_PENDING"
    )
    output.save(
        asset_dir / f"{stem}.png",
        format="PNG",
        optimize=True,
        compress_level=9,
    )

    header = f"""#pragma once

#include <stdint.h>

#define {macro}_IMAGE_WIDTH 135
#define {macro}_IMAGE_HEIGHT 40

extern const uint16_t {stem}_image_rgb565[
    {macro}_IMAGE_WIDTH * {macro}_IMAGE_HEIGHT];
"""
    (include_dir / f"{stem}_image.h").write_text(header, encoding="utf-8")

    values = [rgb565(pixel) for pixel in output.get_flattened_data()]
    rows = []
    for offset in range(0, len(values), 12):
        rows.append(
            "    " + ", ".join(f"0x{value:04x}" for value in values[offset:offset + 12])
        )
    c_source = f"""#include "{stem}_image.h"

const uint16_t {stem}_image_rgb565[
    {macro}_IMAGE_WIDTH * {macro}_IMAGE_HEIGHT] = {{
""" + ",\n".join(rows) + "\n};\n"
    (source_dir / f"{stem}_image.c").write_text(c_source, encoding="utf-8")


if __name__ == "__main__":
    main()
