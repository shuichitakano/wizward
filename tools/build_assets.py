#!/usr/bin/env python3
"""採用画像の収集からPixel Twinsバイナリ検証までを一括実行する。"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def _run(command, cwd: Path) -> None:
    environment = dict(os.environ)
    environment["UV_CACHE_DIR"] = str(Path(tempfile.gettempdir()) / "wizward-asset-uv-cache")
    subprocess.run(command, cwd=cwd, check=True, env=environment)


def main() -> int:
    parser = argparse.ArgumentParser(description="Wizward画像アセットを一括生成する")
    parser.add_argument("--skip-collect", action="store_true", help="プロトタイプからの再収集を省略")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    root = project.parent
    pixel_twins = project / "external" / "pixel-twins"
    if not (pixel_twins / "tools" / "asset_converter").is_dir():
        pixel_twins = root / "pixel-twins"
    converter = pixel_twins / "tools" / "asset_converter"
    if not args.skip_collect:
        _run([sys.executable, "tools/collect_selected_assets.py"], project)
    _run([
        "uv", "run", str(pixel_twins / "tools/font_converter.py"),
        str(project / "assets/source/gameplay/fonts_selected/outlined_8x9_ascii.png"),
        "--header", str(project / "src/assets/wizward_font.hpp"),
        "--source", str(project / "src/assets/wizward_font.cpp"),
        "--symbol", "kWizwardFont",
        "--namespace", "wizward::assets",
        "--columns", "16",
        "--first", "32",
        "--count", "99",
        "--fallback", "63",
        "--outline-color", "#111315",
        "--body-color", "#f1ead8",
        "--outline-index", "1",
        "--sram",
    ], project)
    gameplay = project / "assets" / "converted" / "gameplay"
    title = project / "assets" / "converted" / "title"
    attract = project / "assets" / "converted" / "attract"
    _run([
        "uv", "run", "pixel-twins-assets", str(project / "assets/manifests/gameplay.json"),
        "-o", str(gameplay), "--clean",
    ], converter)
    _run([
        sys.executable, "tools/generate_palette_indices.py", str(gameplay / "report.json"),
        str(gameplay / "palette_indices.hpp"),
    ], project)
    _run([
        "uv", "run", "pixel-twins-assets", str(project / "assets/manifests/title.json"),
        "-o", str(title), "--clean",
    ], converter)
    _run([
        "uv", "run", "pixel-twins-assets", str(project / "assets/manifests/attract.json"),
        "-o", str(attract), "--clean",
    ], converter)
    _run([
        "uv", "run", "pixel-twins-sprites", str(gameplay / "intermediate.json"),
        "-o", str(gameplay / "sprites.bin"), "--namespace", "wizward::assets",
    ], converter)
    _run([
        "uv", "run", "pixel-twins-background", str(project / "assets/manifests/background.json"),
        "-o", str(gameplay / "background.bin"),
    ], converter)
    _run([
        "uv", "run", "pixel-twins-raw-image", str(title / "intermediate.json"),
        "title_screen_selected__title_screen_160x120",
        "-o", str(title / "screen.bin"),
    ], converter)
    for player in ("p1_girl", "p2_boy"):
        _run([
            "uv", "run", "pixel-twins-raw-image", str(attract / "intermediate.json"),
            f"attract_selected__downscaled__ranking_{player}_mage_160x120",
            "-o", str(attract / f"attract_{player}.bin"),
        ], converter)
    _run([sys.executable, "tools/report_asset_memory.py"], project)
    _run([
        "uv", "run", "--with", "pillow", "python", str(project / "tools/verify_converted_assets.py"),
    ], converter)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
