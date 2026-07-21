#!/usr/bin/env python3
"""減色レポートからWizward用の名前付きパレットインデックスを生成する。"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def _constant(name: str) -> str:
    words = re.split(r"[^A-Za-z0-9]+", name)
    return "k" + "".join(word[:1].upper() + word[1:] for word in words if word)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    entries = []
    for entry in report["palette"]:
        if entry["role"] != "reserved" or entry["index"] in (0, 1, 255):
            continue
        entries.append((entry["name"], entry["index"], entry["rgb"], None))
    for target in report.get("palette_targets", []):
        entries.append((target["name"], target["resolved_index"],
                        target["resolved_rgb"], target["desired_rgb"]))

    lines = [
        "// このファイルはgenerate_palette_indices.pyにより生成されました。編集しないでください。",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace wizward::assets::palette {",
        "",
    ]
    for name, index, rgb, desired in entries:
        comment = f"#{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"
        if desired is not None:
            comment += f" (希望 #{desired[0]:02x}{desired[1]:02x}{desired[2]:02x})"
        lines.append(f"inline constexpr std::uint8_t {_constant(name)} = {index}; // {comment}")
    lines.extend(["", "} // namespace wizward::assets::palette", ""])
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
