#!/usr/bin/env python3
"""Fail an Android build if its generated support archive omits equipment icons."""

import sys
import zipfile
from pathlib import Path


REQUIRED_ICONS = (
    "textures/icon_item_custom/gItemIconCaneOfByrnaTex",
    "textures/icon_item_custom/gItemIconFourSwordTex",
    "textures/icon_item_custom/gItemIconDrillshaftTex",
    "textures/icon_item_custom/gItemIconDivineShieldTex",
    "textures/icon_item_custom/gItemIconGerudoScimitarTex",
    "textures/icon_item_custom/gItemIconMagicCapeTex",
    "textures/icon_item_custom/gItemIconPending4Tex",
    "textures/icon_item_custom/gItemIconChampionsTunicTex",
    "textures/icon_item_custom/gItemIconPegasusAnkletTex",
    "textures/icon_item_custom/gItemIconWaterDragonScaleTex",
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} <soh.o2r>", file=sys.stderr)
        return 2

    archive_path = Path(sys.argv[1])
    if not archive_path.is_file():
        print(f"missing support archive: {archive_path}", file=sys.stderr)
        return 1

    try:
        with zipfile.ZipFile(archive_path) as archive:
            entries = {name.removeprefix("__OTR__") for name in archive.namelist()}
    except zipfile.BadZipFile:
        print(f"invalid support archive: {archive_path}", file=sys.stderr)
        return 1

    missing = [icon for icon in REQUIRED_ICONS if icon not in entries]
    if missing:
        print("generated support archive is missing equipment icons:", file=sys.stderr)
        for icon in missing:
            print(f"  {icon}", file=sys.stderr)
        return 1

    print(f"verified {len(REQUIRED_ICONS)} equipment icons in {archive_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
