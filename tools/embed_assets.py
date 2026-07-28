#!/usr/bin/env python3
"""バイナリアセットをFlash配置可能なC++読み取り専用配列へ変換する。"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def _array(name: str, data: bytes, placement: str = "") -> str:
    rows = []
    for offset in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16]) + ",")
    return (
        f"alignas(4) {placement} extern const std::uint8_t {name}[] = {{\n"
        + "\n".join(rows)
        + f"\n}};\nextern const std::size_t {name}Size = sizeof({name});\n"
    )

def _asset_first_pixel_offset(data: bytes, asset_index: int) -> int:
    _, _, _, asset_count, _, _, pixel_data_offset, _ = struct.unpack_from(
        "<4sHHHHIII", data
    )
    if not 0 <= asset_index < asset_count:
        raise ValueError(f"スプライトasset indexが範囲外です: {asset_index}")
    first_frame = struct.unpack_from("<I", data, 24 + asset_index * 12)[0]
    frame_table_offset = 24 + asset_count * 12
    return struct.unpack_from("<I", data, frame_table_offset + first_frame * 8)[0]

def _parse_asset_range(value: str) -> tuple[int, int]:
    try:
        first_text, last_text = value.split(":", 1)
        first, last = int(first_text), int(last_text)
    except ValueError as error:
        raise argparse.ArgumentTypeError("範囲は FIRST:LAST 形式です") from error
    if first < 0 or last < first:
        raise argparse.ArgumentTypeError("スプライト範囲が不正です")
    return first, last


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--background", type=Path, required=True)
    parser.add_argument("--sprites", type=Path, required=True)
    parser.add_argument(
        "--rare-sprite-range",
        type=_parse_asset_range,
        action="append",
        required=True,
    )
    parser.add_argument("--palette", type=Path, required=True)
    parser.add_argument("--title-screen", type=Path, required=True)
    parser.add_argument("--title-palette", type=Path, required=True)
    parser.add_argument("--attract-p1", type=Path, required=True)
    parser.add_argument("--attract-p2", type=Path, required=True)
    parser.add_argument("--attract-palette", type=Path, required=True)
    args = parser.parse_args()
    text = """// tools/embed_assets.pyにより生成されました。編集しないでください。
#include "assets/embedded_assets.hpp"
#include "pixel_twins/platform.hpp"

namespace wizward::assets {

"""
    background = args.background.read_bytes()
    background_header = struct.unpack_from("<4sHHBBBBBBBBIIII", background)
    background_pixel_offset = background_header[12]
    sprites = args.sprites.read_bytes()
    sprite_header = struct.unpack_from("<4sHHHHIII", sprites)
    sprite_pixel_offset = sprite_header[6]
    sprite_pixels = sprites[sprite_pixel_offset:]
    asset_count = sprite_header[3]
    ranges = sorted(args.rare_sprite_range)
    previous_last = -1
    pixel_segments: list[tuple[int, int, bool]] = []
    pixel_cursor = 0
    for first, last in ranges:
        if first <= previous_last or last + 1 >= asset_count:
            raise ValueError("低頻度スプライト範囲が重複または範囲外です")
        rare_start = _asset_first_pixel_offset(sprites, first)
        frequent_start = _asset_first_pixel_offset(sprites, last + 1)
        if not pixel_cursor <= rare_start < frequent_start <= len(sprite_pixels):
            raise ValueError("スプライト画素の分割境界が不正です")
        if rare_start > pixel_cursor:
            pixel_segments.append((pixel_cursor, rare_start, False))
        pixel_segments.append((rare_start, frequent_start, True))
        pixel_cursor = frequent_start
        previous_last = last
    if pixel_cursor < len(sprite_pixels):
        pixel_segments.append((pixel_cursor, len(sprite_pixels), False))
    text += _array("kGameplayBackgroundMetadata", background[:background_pixel_offset]) + "\n"
    text += _array(
        "kGameplayBackgroundPixels",
        background[background_pixel_offset:],
        "PIXEL_TWINS_ASSET_SRAM",
    ) + "\n"
    text += _array(
        "kGameplaySpriteMetadata",
        sprites[:sprite_pixel_offset],
        "PIXEL_TWINS_ASSET_SRAM",
    ) + "\n"
    for index, (start, end, rare) in enumerate(pixel_segments):
        text += _array(
            f"kGameplaySpritePixelsRegion{index}",
            sprite_pixels[start:end],
            "" if rare else "PIXEL_TWINS_ASSET_SRAM",
        ) + "\n"
    text += (
        "extern const pixel_twins::SpritePixelRegion "
        "kGameplaySpritePixelRegions[] = {\n"
    )
    for index, (start, _, _) in enumerate(pixel_segments):
        text += (
            f"    {{{start}U, kGameplaySpritePixelsRegion{index}, "
            f"kGameplaySpritePixelsRegion{index}Size}},\n"
        )
    text += (
        "};\n"
        "extern const std::size_t kGameplaySpritePixelRegionCount = "
        "sizeof(kGameplaySpritePixelRegions) / "
        "sizeof(kGameplaySpritePixelRegions[0]);\n"
    )
    text += _array("kGameplayPaletteData", args.palette.read_bytes())
    text += "\n" + _array("kTitleScreenData", args.title_screen.read_bytes())
    text += "\n" + _array("kTitlePaletteData", args.title_palette.read_bytes())
    text += "\n" + _array("kAttractP1ScreenData", args.attract_p1.read_bytes())
    text += "\n" + _array("kAttractP2ScreenData", args.attract_p2.read_bytes())
    text += "\n" + _array("kAttractPaletteData", args.attract_palette.read_bytes())
    text += "\n} // namespace wizward::assets\n"
    args.output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
