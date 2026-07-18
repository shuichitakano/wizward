#!/usr/bin/env python3
"""生成済みアセットのFlash/SRAM配置容量を集計する。"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    project = args.project.resolve()
    gameplay = project / "assets/converted/gameplay"
    title = project / "assets/converted/title"
    background = (gameplay / "background.bin").read_bytes()
    background_header = struct.unpack_from("<4sHHBBBBBBBBIIII", background)
    background_pixel_offset = background_header[12]
    sprites = (gameplay / "sprites.bin").read_bytes()
    sprite_header = struct.unpack_from("<4sHHHHIII", sprites)
    sprite_pixel_offset = sprite_header[6]
    title_screen = (title / "screen.bin").stat().st_size
    title_sprites = (title / "logo.bin").stat().st_size
    title_palette = (title / "palette.bin").stat().st_size
    gameplay_palette = (gameplay / "palette.bin").stat().st_size

    font_glyphs = 95 * 18
    gameplay_sram = len(sprites) + len(background) - background_pixel_offset + font_glyphs
    flash_only = (
        background_pixel_offset + gameplay_palette
        + title_screen + title_sprites + title_palette
    )
    framebuffer = 320 * 120 * 2
    tilemap = 100 * 100
    terrain_workspace = 100 * 100 // 2
    active_palette = 256 * 3
    steady_sram = gameplay_sram + framebuffer + tilemap + active_palette
    generation_peak = steady_sram + terrain_workspace
    rp2350_sram = 520 * 1024
    report = {
        "format": "wizward-asset-memory-layout",
        "version": 1,
        "rp2350_sram_bytes": rp2350_sram,
        "asset_sram": {
            "background_pixels": len(background) - background_pixel_offset,
            "sprite_metadata": sprite_pixel_offset,
            "sprite_pixels": len(sprites) - sprite_pixel_offset,
            "font_glyphs": font_glyphs,
            "total": gameplay_sram,
        },
        "flash_only": {
            "background_metadata": background_pixel_offset,
            "gameplay_palette_source": gameplay_palette,
            "title_screen": title_screen,
            "title_sprites": title_sprites,
            "title_palette_source": title_palette,
            "total": flash_only,
        },
        "runtime_sram": {
            "framebuffers": framebuffer,
            "tilemap": tilemap,
            "active_palette": active_palette,
            "terrain_generation_workspace": terrain_workspace,
            "steady_total_including_assets": steady_sram,
            "generation_peak_including_assets": generation_peak,
            "remaining_at_generation_peak_before_code_stack_audio_game_state": rp2350_sram - generation_peak,
        },
        "note": "SRAM初期値はFlashにもロードイメージを持つ",
    }
    output = project / "assets/converted/memory_layout.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    markdown = f"""# アセットメモリ配置

| 項目 | bytes | 配置 |
| --- | ---: | --- |
| BG画素 | {report['asset_sram']['background_pixels']:,} | SRAM |
| スプライト記述表 | {report['asset_sram']['sprite_metadata']:,} | SRAM |
| スプライト画素 | {report['asset_sram']['sprite_pixels']:,} | SRAM |
| フォントグリフ | {report['asset_sram']['font_glyphs']:,} | SRAM |
| BG参照表 | {report['flash_only']['background_metadata']:,} | Flash |
| gameplayパレット原本 | {gameplay_palette:,} | Flash |
| タイトル一枚絵 | {title_screen:,} | Flash |
| タイトルロゴ | {title_sprites:,} | Flash |
| タイトルパレット原本 | {title_palette:,} | Flash |

ゲーム画像のSRAM配置は合計{gameplay_sram:,} bytesです。ダブルフレームバッファ、タイルマップ、
現在パレットを含む定常使用量は{steady_sram:,} bytes、地形生成時ピークは{generation_peak:,} bytesです。
RP2350の520 KiBから差し引くと、コード、スタック、音声、ゲーム状態用に{rp2350_sram - generation_peak:,} bytes残ります。

SRAM配置データの初期値は、起動時コピー元としてFlashにも同容量のロードイメージを持ちます。
"""
    output.with_suffix(".md").write_text(markdown, encoding="utf-8")
    print(f"asset SRAM={gameplay_sram}, generation peak={generation_peak}, remaining={rp2350_sram - generation_peak}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
