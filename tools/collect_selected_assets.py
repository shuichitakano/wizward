#!/usr/bin/env python3
"""Wizwardプロトタイプから本番採用画像を収集し、変換マニフェストを生成する。"""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


IMAGE_REFERENCE_RE = re.compile(r'assets/[A-Za-z0-9_./-]+\.png')
FRAME_RE = re.compile(r"(\d+)x(\d+)_\d+f")

FRAME_RESERVED = [
    (2, "effect fire", "#ff7b32"),
    (3, "effect wind", "#79f05b"),
    (4, "effect thunder", "#ffe35a"),
    (5, "effect ice", "#8de9ff"),
    (6, "effect orb", "#9b7cff"),
    (7, "effect familiar", "#ff8bd1"),
    (8, "effect speed", "#39e6d0"),
    (9, "effect hp", "#ff4c6d"),
    (10, "effect heal", "#57f287"),
    (11, "effect gold", "#ffcf5a"),
    (12, "effect energy blue", "#5ec8ff"),
    (13, "effect energy pale", "#e8fbff"),
    (14, "effect damage", "#ff3d5a"),
    (15, "effect highlight", "#ffd96a"),
    (16, "effect success", "#70ff9b"),
    (17, "effect magic", "#b88cff"),
]

UI_RESERVED = [
    (18, "font body default", "#f1ead8"),
    (19, "player 1 marker", "#ff7648"),
    (20, "player 2 marker", "#69a8ff"),
    (21, "ranking title", "#8fd7ff"),
    (22, "result down", "#ff8b7a"),
    (23, "result time up", "#ffb86b"),
    (24, "seal cyan", "#61dff2"),
    (25, "seal deep cyan", "#286b8d"),
    (26, "seal pale cyan", "#dffcff"),
    (27, "panel dark", "#111816"),
    (28, "gauge dark", "#141716"),
    (29, "collision map", "#425245"),
    (30, "minimap field", "#315344"),
    (31, "ui beige outline", "#f4d8a6"),
]

BG_TERRAINS = ["water", "sand", "grass", "dirt", "road", "plaza"]
BG_BOUNDARIES = [
    "water_to_sand",
    "sand_to_grass",
    "grass_to_dirt",
    "dirt_to_road",
    "dirt_to_plaza",
    "road_to_plaza",
]
BG_OBJECTS = [
    "rock_grass_0",
    "grass_clump_grass_0",
    "flower_white_grass_0",
    "flower_blue_grass_0",
    "flower_yellow_grass_0",
    "shrub_grass_0",
    "plaza_pedestal_0",
    "seal_inactive_plaza_0",
    "seal_active_plaza_0",
]
BG_VARIANTS = 4
BG_MASKS = 16


def _runtime_image_references(prototype: Path) -> List[str]:
    source = (prototype / "app.js").read_text(encoding="utf-8")
    references = {
        match
        for match in IMAGE_REFERENCE_RE.findall(source)
        if "_selected/" in match and "title_screen_selected/" not in match
    }
    return sorted(references)


def _map_image_references(prototype: Path) -> List[str]:
    root = "assets/map_tiles_selected"
    manifest = json.loads((prototype / root / "selected_assets.json").read_text(encoding="utf-8"))
    missing_terrains = sorted(set(BG_TERRAINS) - set(manifest["terrain"]["ids"]))
    missing_boundaries = sorted(set(BG_BOUNDARIES) - set(manifest["boundaries"]["ids"]))
    missing_objects = sorted(set(BG_OBJECTS) - set(manifest["objects_baked"]["ids"]))
    if missing_terrains or missing_boundaries or missing_objects:
        raise ValueError(
            f"BG採用定義が不足しています: terrain={missing_terrains}, "
            f"boundary={missing_boundaries}, object={missing_objects}"
        )
    references = []
    for terrain_id in BG_TERRAINS:
        references.extend(f"{root}/terrain/{terrain_id}_{variant}.png" for variant in range(BG_VARIANTS))
    for boundary_id in BG_BOUNDARIES:
        references.extend(f"{root}/boundaries/{boundary_id}_{mask:02d}.png" for mask in range(BG_MASKS))
    references.extend(f"{root}/objects_baked/{object_id}.png" for object_id in BG_OBJECTS)
    return references


def _background_asset_id(relative: str) -> str:
    return _asset_id(Path(relative).relative_to("assets").as_posix())


def _background_pack_config() -> Dict[str, object]:
    root = "assets/map_tiles_selected"
    return {
        "version": 1,
        "intermediate": "../converted/gameplay/intermediate.json",
        "tile_width": 32,
        "tile_height": 32,
        "max_patterns": 128,
        "variants_per_terrain": BG_VARIANTS,
        "masks_per_boundary": BG_MASKS,
        "cpp_namespace": "wizward::assets",
        "terrains": [
            {
                "id": terrain,
                "assets": [
                    _background_asset_id(f"{root}/terrain/{terrain}_{variant}.png")
                    for variant in range(BG_VARIANTS)
                ],
            }
            for terrain in BG_TERRAINS
        ],
        "boundaries": [
            {
                "id": boundary,
                "assets": [
                    _background_asset_id(f"{root}/boundaries/{boundary}_{mask:02d}.png")
                    for mask in range(BG_MASKS)
                ],
            }
            for boundary in BG_BOUNDARIES
        ],
        "objects": [
            {
                "id": object_id.removesuffix("_0"),
                "assets": [_background_asset_id(f"{root}/objects_baked/{object_id}.png")],
            }
            for object_id in BG_OBJECTS
        ],
    }


def _asset_id(relative: str) -> str:
    path = Path(relative)
    return "__".join(path.with_suffix("").parts)


def _asset_layout(
    relative: Path, set_name: str
) -> Tuple[str, str, Optional[Dict[str, int]]]:
    if relative.parts[:2] == ("map_tiles_selected", "overlays"):
        return "sprite", "sprite", {"width": 32, "height": 32}
    if relative.parts[0] == "map_tiles_selected" or relative.name == "title_screen_160x120.png":
        return "background", "background", None
    if relative.parts[0] == "fonts_selected":
        return "sprite", "font", {"width": 8, "height": 9}
    if relative.name == "powerup_icons_16_sheet.png":
        return "sprite", "sprite", {"width": 16, "height": 16}
    if relative.name == "powerup_icons_8_sheet.png":
        return "sprite", "sprite", {"width": 8, "height": 8}
    if relative.name == "wizward_logo_104x20.png":
        return "sprite", "sprite", {"width": 104, "height": 20}
    matches = list(FRAME_RE.finditer(relative.name))
    if not matches:
        raise ValueError(f"フレーム寸法を判定できません: {relative}")
    match = matches[-1]
    return "sprite", "sprite", {"width": int(match.group(1)), "height": int(match.group(2))}


def _copy_and_describe(
    prototype: Path, project: Path, references: Iterable[str], set_name: str
) -> List[Dict[str, object]]:
    assets = []
    for reference in sorted(set(references)):
        source = prototype / reference
        if not source.is_file():
            raise FileNotFoundError(f"採用アセットがありません: {source}")
        relative = Path(reference).relative_to("assets")
        destination = project / "assets" / "source" / set_name / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        kind, pack_kind, frame = _asset_layout(relative, set_name)
        assets.append(
            {
                "id": _asset_id(relative.as_posix()),
                "kind": kind,
                "pack_kind": pack_kind,
                "frame": frame,
                "source": f"../source/{set_name}/{relative.as_posix()}",
                "alpha_threshold": 0,
                "palette_weight": 1 if kind == "background" else 4,
                "adopted_from": reference,
            }
        )
    return assets


def _converter_manifest(name: str, assets: List[Dict[str, object]], gameplay: bool) -> Dict[str, object]:
    reserved = FRAME_RESERVED + UI_RESERVED if gameplay else []
    return {
        "version": 1,
        "name": name,
        "palette": {
            "asset_range": [32, 254] if gameplay else [2, 254],
            "sample_pixels": 1_000_000,
            "max_pixels_per_asset": 4096,
            "quantizer": "fast_octree" if gameplay else "median_cut",
            "reserved": [
                {"index": index, "name": label, "color": color}
                for index, label, color in reserved
            ],
        },
        "assets": assets,
    }


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def collect(prototype: Path, project: Path) -> Tuple[int, int]:
    for set_name in ("gameplay", "title"):
        source_root = project / "assets" / "source" / set_name
        if source_root.exists():
            shutil.rmtree(source_root)
    gameplay_references = _runtime_image_references(prototype)
    gameplay_references.extend(_map_image_references(prototype))
    title_references = [
        "assets/title_screen_selected/title_screen_160x120.png",
        "assets/title_screen_selected/wizward_logo_104x20.png",
    ]

    gameplay = _copy_and_describe(prototype, project, gameplay_references, "gameplay")
    title = _copy_and_describe(prototype, project, title_references, "title")
    _write_json(
        project / "assets/manifests/gameplay.json",
        _converter_manifest("wizward-gameplay", gameplay, gameplay=True),
    )
    _write_json(
        project / "assets/manifests/title.json",
        _converter_manifest("wizward-title", title, gameplay=False),
    )
    _write_json(project / "assets/manifests/background.json", _background_pack_config())
    inventory = {
        "version": 1,
        "source_repository": prototype.name,
        "sets": {
            "gameplay": [asset["adopted_from"] for asset in gameplay],
            "title": [asset["adopted_from"] for asset in title],
        },
    }
    _write_json(project / "assets/adopted_inventory.json", inventory)
    return len(gameplay), len(title)


def main() -> int:
    parser = argparse.ArgumentParser(description="Wizwardの採用画像を本番リポジトリへ収集する")
    parser.add_argument(
        "--prototype",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "wizward-prototype",
    )
    parser.add_argument(
        "--project",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    gameplay_count, title_count = collect(args.prototype.resolve(), args.project.resolve())
    print(f"collected gameplay={gameplay_count}, title={title_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
