#!/usr/bin/env python3
"""収集元と変換済み中間画像の整合性を検証する。"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from PIL import Image


BG_HEADER_FORMAT = "<4sHHBBBBBBBBIIII"
BG_HEADER_SIZE = struct.calcsize(BG_HEADER_FORMAT)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _verify_sprite_pack(output: Path, intermediate: dict, report_stem: str = "sprites") -> None:
    report = json.loads((output / f"{report_stem}.json").read_text(encoding="utf-8"))
    binary = (output / report["binary"]).read_bytes()
    if hashlib.sha256(binary).hexdigest() != report["sha256"]:
        raise ValueError("スプライトバイナリのSHA-256が一致しません")
    header = struct.unpack_from("<4sHHHHIII", binary)
    if header[0] != b"PTSP" or header[1] != 1 or header[2] != 24 or header[4] != 12:
        raise ValueError("スプライトバイナリのヘッダーが不正です")
    asset_count, frame_count, pixel_data_offset, pixel_data_size = header[3], header[5], header[6], header[7]
    if asset_count != len(report["assets"]) or len(binary) != pixel_data_offset + pixel_data_size:
        raise ValueError("スプライトバイナリのサイズまたはアセット数が不正です")
    intermediate_by_id = {asset["id"]: asset for asset in intermediate["assets"]}
    frame_table_offset = 24 + asset_count * 12
    parsed_frames = 0
    for packed_asset in report["assets"]:
        asset_index = packed_asset["asset_index"]
        descriptor = struct.unpack_from("<IHBBBBH", binary, 24 + asset_index * 12)
        first_frame, local_count, columns, rows, logical_width, logical_height, _ = descriptor
        if first_frame != parsed_frames or local_count != packed_asset["frame_count"]:
            raise ValueError(f"フレーム表が不正です: {packed_asset['id']}")
        source_asset = intermediate_by_id[packed_asset["id"]]
        with Image.open(output / source_asset["indexed_png"]) as sheet:
            for local_index in range(local_count):
                frame = struct.unpack_from(
                    "<IBBBB", binary, frame_table_offset + (first_frame + local_index) * 8
                )
                pixel_offset, trim_x, trim_y, width, height = frame
                column = local_index % columns
                row = local_index // columns
                expected = list(
                    sheet.crop(
                        (
                            column * logical_width,
                            row * logical_height,
                            (column + 1) * logical_width,
                            (row + 1) * logical_height,
                        )
                    ).getdata()
                )
                restored = [0] * (logical_width * logical_height)
                if width and height:
                    body_start = pixel_data_offset + pixel_offset
                    body = binary[body_start : body_start + width * height]
                    for y in range(height):
                        target = (trim_y + y) * logical_width + trim_x
                        restored[target : target + width] = body[y * width : (y + 1) * width]
                if restored != expected:
                    raise ValueError(f"trim復元結果が一致しません: {packed_asset['id']} frame {local_index}")
        parsed_frames += local_count
    if parsed_frames != frame_count:
        raise ValueError("総フレーム数が一致しません")


def _verify_raw_image(output: Path, intermediate: dict) -> None:
    report = json.loads((output / "screen.json").read_text(encoding="utf-8"))
    binary = (output / report["binary"]).read_bytes()
    if hashlib.sha256(binary).hexdigest() != report["sha256"]:
        raise ValueError("タイトル画面バイナリのSHA-256が一致しません")
    asset = next(item for item in intermediate["assets"] if item["id"] == report["asset_id"])
    with Image.open(output / asset["indexed_png"]) as image:
        if image.size != (report["width"], report["height"]) or bytes(image.getdata()) != binary:
            raise ValueError("タイトル画面バイナリが変換画像と一致しません")


def _verify_palette(output: Path, report: dict) -> None:
    palette = (output / report["palette_binary"]["file"]).read_bytes()
    expected = bytes(channel for entry in report["palette"] for channel in entry["rgb"])
    if palette != expected or len(palette) != 768:
        raise ValueError(f"{output.name}: palette.binがレポートと一致しません")
    if hashlib.sha256(palette).hexdigest() != report["palette_binary"]["sha256"]:
        raise ValueError(f"{output.name}: palette.binのSHA-256が一致しません")
    candidates = [
        entry for entry in report["palette"]
        if entry["index"] != 0 and entry["role"] != "unused"
    ]
    for target in report.get("palette_targets", []):
        desired = target["desired_rgb"]
        nearest = min(candidates, key=lambda entry: sum(
            (entry["rgb"][channel] - desired[channel]) ** 2 for channel in range(3)
        ))
        if target["resolved_index"] != nearest["index"] or target["resolved_rgb"] != nearest["rgb"]:
            raise ValueError(f"{output.name}: 目標色の最近傍割り当てが不正です: {target['name']}")


def _verify_gameplay_palette_contract(output: Path, report: dict) -> None:
    expected = {
        2: "font_body", 3: "player_1", 4: "player_2", 5: "hp_gauge",
        6: "highlight", 7: "ranking_title", 8: "gauge_dark",
        9: "minimap_collision", 10: "minimap_field", 11: "minimap_border",
        12: "minimap_landmark", 13: "boss_gauge_empty",
    }
    actual = {
        entry["index"]: entry["name"] for entry in report["palette"]
        if entry["role"] == "reserved" and entry["index"] not in (0, 1, 255)
    }
    if actual != expected:
        raise ValueError(f"固定UIパレット契約が不正です: {actual}")
    header = (output / "palette_indices.hpp").read_text(encoding="utf-8")
    for index, name in expected.items():
        constant = "k" + "".join(part.capitalize() for part in name.split("_"))
        if f"{constant} = {index};" not in header:
            raise ValueError(f"固定UIパレット定数が不正です: {constant}")
    for target in report["palette_targets"]:
        constant = "k" + "".join(part.capitalize() for part in target["name"].split("_"))
        if f"{constant} = {target['resolved_index']};" not in header:
            raise ValueError(f"演出パレット定数が不正です: {constant}")


def _verify_background_pack(output: Path, intermediate: dict) -> None:
    report = json.loads((output / "background.json").read_text(encoding="utf-8"))
    binary = (output / report["binary"]).read_bytes()
    if hashlib.sha256(binary).hexdigest() != report["sha256"]:
        raise ValueError("BGバイナリのSHA-256が一致しません")
    header = struct.unpack_from(BG_HEADER_FORMAT, binary)
    if header[0] != b"PTBG" or header[1] != 1 or header[2] != BG_HEADER_SIZE:
        raise ValueError("BGバイナリのヘッダーが不正です")
    tile_width, tile_height, pattern_count = header[3], header[4], header[5]
    terrain_count, variants = header[6], header[7]
    boundary_count, masks, object_count = header[8], header[9], header[10]
    mapping_offset, pattern_offset, pattern_size = header[11], header[12], header[13]
    mapping_count = terrain_count * variants + boundary_count * masks + object_count
    if pattern_count > 128 or pattern_size != pattern_count * tile_width * tile_height:
        raise ValueError("BGパターン数または画素容量が不正です")
    if len(binary) != pattern_offset + pattern_size or pattern_offset - (mapping_offset + mapping_count) > 3:
        raise ValueError("BGバイナリのオフセットまたはサイズが不正です")
    mapping = binary[mapping_offset : mapping_offset + mapping_count]
    if any(index >= pattern_count for index in mapping):
        raise ValueError("BGマッピングがパターン範囲外です")
    assets_by_id = {asset["id"]: asset for asset in intermediate["assets"]}
    logical = [pattern for group in report["terrains"] for pattern in group["patterns"]]
    logical += [pattern for group in report["boundaries"] for pattern in group["patterns"]]
    logical += [group["pattern"] for group in report["objects"]]
    if len(logical) != mapping_count:
        raise ValueError("BGレポートの論理パターン数が一致しません")
    for logical_index, item in enumerate(logical):
        if mapping[logical_index] != item["pattern_index"]:
            raise ValueError(f"BGマッピングがレポートと一致しません: {item['id']}")
        asset = assets_by_id[item["id"]]
        with Image.open(output / asset["indexed_png"]) as image:
            expected = bytes(image.getdata())
        start = pattern_offset + item["pattern_index"] * tile_width * tile_height
        if binary[start : start + len(expected)] != expected:
            raise ValueError(f"BGパターン画素が一致しません: {item['id']}")
    header_path = output / report["header"]
    if not header_path.is_file():
        raise ValueError("BGアセットIDヘッダーがありません")


def verify(prototype: Path, project: Path) -> int:
    inventory_path = project / "assets/adopted_inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    checked = 0
    for set_name, references in inventory["sets"].items():
        for reference in references:
            relative = Path(reference).relative_to("assets")
            original = prototype / reference
            collected = project / "assets/source" / set_name / relative
            if _sha256(original) != _sha256(collected):
                raise ValueError(f"収集画像が採用元と一致しません: {reference}")
            checked += 1

        output = project / "assets/converted" / set_name
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        _verify_palette(output, report)
        if report["summary"]["asset_count"] != len(references):
            raise ValueError(f"{set_name}: インベントリと変換数が一致しません")
        expected_palette = [channel for entry in report["palette"] for channel in entry["rgb"]]
        for asset in report["assets"]:
            source_path = (output / asset["source"]).resolve()
            indexed_path = output / asset["indexed_png"]
            with Image.open(source_path) as source, Image.open(indexed_path) as indexed:
                if indexed.mode != "P":
                    raise ValueError(f"Pモードではありません: {indexed_path}")
                if indexed.getpalette() != expected_palette:
                    raise ValueError(f"共通パレットと一致しません: {indexed_path}")
                if indexed.size != source.size:
                    raise ValueError(f"画像寸法が変化しています: {indexed_path}")
                if asset["kind"] == "sprite":
                    threshold = asset["alpha_threshold"]
                    source_alpha = [a <= threshold for _, _, _, a in source.convert("RGBA").getdata()]
                    indexed_transparent = [value == 0 for value in indexed.getdata()]
                    if source_alpha != indexed_transparent:
                        raise ValueError(f"透過インデックスが一致しません: {indexed_path}")
        if set_name == "gameplay":
            _verify_gameplay_palette_contract(output, report)
            _verify_sprite_pack(output, report)
            _verify_background_pack(output, report)
        else:
            _verify_sprite_pack(output, report, "logo")
            _verify_raw_image(output, report)
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description="Wizward変換済み画像の整合性を検証する")
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
    count = verify(args.prototype.resolve(), args.project.resolve())
    print(f"verified {count} adopted assets and converted palette sets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
