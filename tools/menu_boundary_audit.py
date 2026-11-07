#!/usr/bin/env python3
"""Keep the main-menu ABI free of gameplay HUD state; run as part of make test."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPORTS = {"Init", "Shutdown", "Refresh", "KeyEvent", "TextInput", "MouseEvent", "GamepadNavigate", "UpdateLobbySetup"}
FORBIDDEN = re.compile(r"\b(?:LPCPLAYER|LPPLAYER|playerState_t|entityState_t|menuUnitData_t|UpdateUnitUI|UpdatePlayerState|game_mode)\b")


def audit(root=ROOT):
    errors = []
    header = (root / "client/menu.h").read_text()
    exports = header.split("/* Function table exported", 1)[1].split("} menuExport_t", 1)[0]
    names = set(re.findall(r"\(\*(\w+)\)", exports))
    if names != EXPORTS:
        errors.append(f"menuExport_t changed: added {sorted(names - EXPORTS)}, missing {sorted(EXPORTS - names)}")
    paths = [root / "client/menu.h"]
    for folder in (root / "games").glob("*/menu"):
        paths.extend(p for p in folder.rglob("*") if p.suffix in {".c", ".h"})
    for path in paths:
        for num, line in enumerate(path.read_text().splitlines(), 1):
            if FORBIDDEN.search(line):
                errors.append(f"{path.relative_to(root)}:{num}: gameplay state in main-menu module: {line.strip()}")
    return errors


if __name__ == "__main__":
    errors = audit()
    print("\n".join(errors) if errors else "menu-boundary-audit: clean")
    raise SystemExit(bool(errors))
