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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--background", type=Path, required=True)
    parser.add_argument("--sprites", type=Path, required=True)
    parser.add_argument("--palette", type=Path, required=True)
    parser.add_argument("--title-screen", type=Path, required=True)
    parser.add_argument("--title-sprites", type=Path, required=True)
    parser.add_argument("--title-palette", type=Path, required=True)
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
    text += _array(
        "kGameplaySpritePixels",
        sprites[sprite_pixel_offset:],
        "PIXEL_TWINS_ASSET_SRAM",
    ) + "\n"
    text += _array("kGameplayPaletteData", args.palette.read_bytes())
    text += "\n" + _array("kTitleScreenData", args.title_screen.read_bytes())
    text += "\n" + _array("kTitleSpriteData", args.title_sprites.read_bytes())
    text += "\n" + _array("kTitlePaletteData", args.title_palette.read_bytes())
    text += "\n} // namespace wizward::assets\n"
    args.output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
