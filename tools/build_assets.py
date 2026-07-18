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
    converter = root / "pixel-twins" / "tools" / "asset_converter"
    if not args.skip_collect:
        _run([sys.executable, "tools/collect_selected_assets.py"], project)
    gameplay = project / "assets" / "converted" / "gameplay"
    title = project / "assets" / "converted" / "title"
    _run([
        "uv", "run", "pixel-twins-assets", str(project / "assets/manifests/gameplay.json"),
        "-o", str(gameplay), "--clean",
    ], converter)
    _run([
        "uv", "run", "pixel-twins-assets", str(project / "assets/manifests/title.json"),
        "-o", str(title), "--clean",
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
        "uv", "run", "pixel-twins-sprites", str(title / "intermediate.json"),
        "-o", str(title / "logo.bin"), "--header", str(title / "title_sprites.hpp"),
        "--namespace", "wizward::title_assets",
    ], converter)
    _run([
        "uv", "run", "pixel-twins-raw-image", str(title / "intermediate.json"),
        "title_screen_selected__title_screen_160x120",
        "-o", str(title / "screen.bin"),
    ], converter)
    _run([sys.executable, "tools/report_asset_memory.py"], project)
    _run([
        "uv", "run", "--with", "pillow", "python", str(project / "tools/verify_converted_assets.py"),
    ], converter)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
