#!/usr/bin/env python3
# MIT License
#
# Copyright (c) 2026 sookyboo
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import argparse
import os
import platform
import shutil
import struct
import sys
import time
import zlib
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Any


# ----------------------------
# AppID (SRM-compatible)
# ----------------------------
def shortcut_appid(exe: str, app_name: str) -> int:
    """
    SRM "short app id" (top 32 bits): crc32(exe+appname) | 0x80000000
    """
    combined = (exe + app_name).encode("utf-8")
    crc = zlib.crc32(combined) & 0xFFFFFFFF
    return crc | 0x80000000


def shortcut_appid_signed(exe: str, app_name: str) -> int:
    """
    SRM shortcuts.vdf appid (signed int32):
      (top32) - 0x100000000
    """
    top32 = shortcut_appid(exe, app_name) & 0xFFFFFFFF
    return int(top32) - 0x100000000


def shortcut_long_appid(appid32: int) -> int:
    """
    SRM long app id (Big Picture / artwork key):
      (top32 << 32) | 0x02000000
    """
    return ((int(appid32) & 0xFFFFFFFF) << 32) | 0x02000000


# ----------------------------
# SteamID mapping (Steam Deck controller configs)
# ----------------------------
STEAMID64_BASE = 76561197960265728


def controller_config_id(steamid: str) -> str:
    """
    Steam stores controller configs under:
      steamapps/common/Steam Controller Configs/<accountid>/config/...
    while userdata folders are often SteamID64:
      userdata/<steamid64>/...
    This helper returns <accountid> when given a SteamID64, otherwise returns input as-is.
    """
    s = str(steamid or "").strip()
    if not s.isdigit():
        return s
    try:
        v = int(s, 10)
    except Exception:
        return s

    # Heuristic: SteamID64 values are large and often start with 7656...
    if len(s) >= 16 and s.startswith("7656"):
        acct = v - STEAMID64_BASE
        if acct >= 0:
            return str(acct)
    return s


# ----------------------------
# Steam path detection
# ----------------------------
def steam_root_candidates() -> List[Path]:
    system = platform.system().lower()
    candidates: List[Path] = []
    home = Path.home()

    if "windows" in system:
        pf86 = os.environ.get("PROGRAMFILES(X86)")
        pf = os.environ.get("PROGRAMFILES")
        if pf86:
            candidates.append(Path(pf86) / "Steam")
        if pf:
            candidates.append(Path(pf) / "Steam")
        candidates.append(Path("C:/Steam"))
    else:
        # Flatpak remaps a host xdg-data/Steam filesystem grant beneath the
        # sandbox's XDG_DATA_HOME. Check that location first so native Steam on
        # SteamOS/desktop Linux remains discoverable from inside the sandbox.
        xdg_data_home = os.environ.get("XDG_DATA_HOME")
        if xdg_data_home:
            candidates.append(Path(xdg_data_home) / "Steam")

        candidates.extend([
            home / ".steam" / "steam",
            home / ".local" / "share" / "Steam",
            home / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
            ])

    # A candidate can be reachable through more than one alias/symlink. Preserve
    # priority while avoiding duplicate probes and duplicate diagnostic output.
    unique: List[Path] = []
    seen = set()
    for candidate in candidates:
        key = str(candidate)
        if key not in seen:
            seen.add(key)
            unique.append(candidate)
    return unique


def detect_steam_root() -> Optional[Path]:
    for candidate in steam_root_candidates():
        if (candidate / "userdata").is_dir():
            return candidate
    return None


def choose_userdata_dir(steam_root: Path, steamid: Optional[str]) -> Tuple[Path, str]:
    userdata = steam_root / "userdata"
    if not userdata.is_dir():
        raise FileNotFoundError(f"No userdata dir under {steam_root}")

    if steamid:
        d = userdata / steamid
        if not d.is_dir():
            raise FileNotFoundError(f"SteamID folder not found: {d}")
        return d, steamid

    best_dir: Optional[Path] = None
    best_id: Optional[str] = None
    best_mtime = -1.0

    for d in userdata.iterdir():
        if not d.is_dir() or not d.name.isdigit():
            continue
        vdf = d / "config" / "shortcuts.vdf"
        if vdf.exists():
            mtime = vdf.stat().st_mtime
            if mtime > best_mtime:
                best_mtime = mtime
                best_dir = d
                best_id = d.name

    if best_dir is None:
        for d in userdata.iterdir():
            if d.is_dir() and d.name.isdigit():
                mtime = d.stat().st_mtime
                if mtime > best_mtime:
                    best_mtime = mtime
                    best_dir = d
                    best_id = d.name

    if best_dir is None or best_id is None:
        raise FileNotFoundError(f"Could not find any SteamID folders under {userdata}")

    return best_dir, best_id


def list_all_steamids(steam_root: Path) -> List[str]:
    userdata = steam_root / "userdata"
    if not userdata.is_dir():
        return []
    ids: List[str] = []
    for d in userdata.iterdir():
        if d.is_dir() and d.name.isdigit():
            ids.append(d.name)
    return ids

def detect_recent_steamid() -> Tuple[Path, str]:
    steam_root = detect_steam_root()
    if not steam_root:
        raise FileNotFoundError("Could not auto-detect Steam root")
    _userdir, sid = choose_userdata_dir(steam_root, None)
    return steam_root, sid


def is_flatpak_shortcut_installed(steam_root: Path, steamid: str, flatpak_app_id: str) -> bool:
    vdf = steam_root / "userdata" / steamid / "config" / "shortcuts.vdf"
    if not vdf.exists():
        return False

    data = vdf.read_bytes()
    count, ok = _verify_shortcuts_vdf_roundtrip(data)
    if not ok:
        return False

    shortcuts = parse_shortcuts_vdf(data)
    for s in shortcuts:
        if (s.flatpak_app_id or "") == (flatpak_app_id or ""):
            return True
    return False

# ----------------------------
# Binary VDF parsing/writing (shortcuts.vdf) - steam-shortcut-editor compatible
# ----------------------------
KV_OBJECT = 0x00
KV_STRING = 0x01
KV_INT = 0x02
KV_END = 0x08
KV_NUL = 0x00


def _kv_read_cstring(buf: bytes, i: int) -> Tuple[str, int]:
    j = buf.find(b"\x00", i)
    if j < 0:
        raise ValueError("Unterminated cstring")
    return buf[i:j].decode("utf-8", errors="replace"), j + 1


def _kv_parse_object(buf: bytes, i: int, auto_arrays: bool, auto_bools: bool) -> Tuple[Any, int]:
    obj: Dict[str, Any] = {}
    while i < len(buf):
        t = buf[i]
        i += 1
        if t == KV_END:
            break

        key, i = _kv_read_cstring(buf, i)

        if t == KV_OBJECT:
            val, i = _kv_parse_object(buf, i, auto_arrays, auto_bools)
        elif t == KV_STRING:
            val, i = _kv_read_cstring(buf, i)
        elif t == KV_INT:
            if i + 4 > len(buf):
                raise ValueError("Unexpected EOF reading int32")
            val = struct.unpack_from("<i", buf, i)[0]
            i += 4
            if auto_bools and (val == 0 or val == 1):
                val = bool(val)
        else:
            raise ValueError(f"Unknown KV type byte: 0x{t:02x}")

        obj[key] = val

    if auto_arrays:
        keys = list(obj.keys())
        if keys and all(k.isdigit() for k in keys):
            max_idx = max(int(k) for k in keys)
            arr: List[Any] = [None] * (max_idx + 1)
            for k, v in obj.items():
                arr[int(k)] = v
            return arr, i

    return obj, i


def kv_parse(buf: bytes, auto_arrays: bool = True, auto_bools: bool = True) -> Any:
    """
    Matches steam-shortcut-editor parseBuffer default options:
      autoConvertArrays: true
      autoConvertBooleans: true
    """
    val, _i = _kv_parse_object(buf, 0, auto_arrays, auto_bools)
    return val


def _kv_write_string(s: str, out: bytearray) -> None:
    b = (s or "").encode("utf-8")
    out += b
    out.append(KV_NUL)


def _kv_append_value(val: Any, out: bytearray) -> None:
    # Pre-process (match steam-shortcut-editor)
    if val is None:
        val = ""
    elif isinstance(val, bool):
        val = 1 if val else 0
    elif isinstance(val, (int,)):
        pass
    elif isinstance(val, float):
        val = int(val)
    elif isinstance(val, str):
        pass
    elif isinstance(val, (list, dict)):
        pass
    else:
        val = str(val)

    if isinstance(val, str):
        _kv_write_string(val, out)
        return

    if isinstance(val, int):
        out += struct.pack("<i", int(val))
        return

    # object/array
    if isinstance(val, list):
        keys = [str(i) for i in range(len(val))]
        getter = lambda k: val[int(k)]
    else:
        keys = list(val.keys())
        getter = lambda k: val[k]

    for k in keys:
        prop = getter(k)
        if prop is None or isinstance(prop, str):
            t = KV_STRING
        elif isinstance(prop, bool) or isinstance(prop, int) or isinstance(prop, float):
            t = KV_INT
        else:
            t = KV_OBJECT

        out.append(t)
        _kv_write_string(str(k), out)
        _kv_append_value(prop, out)

    out.append(KV_END)


def kv_write(obj: Any) -> bytes:
    out = bytearray()
    _kv_append_value(obj, out)
    return bytes(out)


def _caseless_get(d: Dict[str, Any], key: str, default: Any = "") -> Any:
    kl = key.lower()
    for k, v in d.items():
        if str(k).lower() == kl:
            return v
    return default


def _caseless_set(d: Dict[str, Any], preferred_key: str, value: Any) -> None:
    """
    SRM-ish behavior: if the key exists (case-insensitive), update in-place.
    If missing, insert new keys at the end (matches JS insertion-order behavior).
    """
    kl = preferred_key.lower()
    for k in list(d.keys()):
        if str(k).lower() == kl:
            d[k] = value
            return
    d[preferred_key] = value


@dataclass
class Shortcut:
    appid: int
    app_name: str
    exe: str
    start_dir: str = ""
    icon: str = ""
    shortcut_path: str = ""
    launch_options: str = ""
    is_hidden: bool = False
    allow_desktop_config: bool = True
    allow_overlay: bool = True
    open_vr: int = 0
    devkit: int = 0
    devkit_game_id: str = ""
    devkit_override_appid: int = 0
    last_play_time: int = 0
    tags: List[str] = field(default_factory=list)

    flatpak_app_id: str = ""
    kv: Dict[str, Any] = field(default_factory=dict)


def parse_shortcuts_vdf(data: bytes) -> List[Shortcut]:
    root = kv_parse(data, auto_arrays=True, auto_bools=True)
    if not isinstance(root, dict) or "shortcuts" not in root:
        raise ValueError("Not a shortcuts.vdf (missing 'shortcuts' root key)")

    sc_list = root.get("shortcuts", [])
    if isinstance(sc_list, dict):
        keys = [k for k in sc_list.keys() if str(k).isdigit()]
        sc_list = [sc_list[str(i)] for i in range(len(keys)) if str(i) in sc_list]
    if not isinstance(sc_list, list):
        raise ValueError("shortcuts.vdf: 'shortcuts' is not a list")

    out: List[Shortcut] = []
    for item in sc_list:
        if not isinstance(item, dict):
            continue

        sc = Shortcut(
            appid=int(_caseless_get(item, "appid", 0) or 0),
            app_name=str(_caseless_get(item, "AppName", _caseless_get(item, "appname", "")) or ""),
            exe=str(_caseless_get(item, "exe", _caseless_get(item, "Exe", "")) or ""),
            start_dir=str(_caseless_get(item, "StartDir", _caseless_get(item, "startdir", "")) or ""),
            icon=str(_caseless_get(item, "icon", "") or ""),
            shortcut_path=str(_caseless_get(item, "ShortcutPath", _caseless_get(item, "shortcutpath", "")) or ""),
            launch_options=str(_caseless_get(item, "LaunchOptions", _caseless_get(item, "launchoptions", "")) or ""),
            is_hidden=bool(_caseless_get(item, "IsHidden", _caseless_get(item, "ishidden", False))),
            allow_desktop_config=bool(_caseless_get(item, "AllowDesktopConfig", _caseless_get(item, "allowdesktopconfig", True))),
            allow_overlay=bool(_caseless_get(item, "AllowOverlay", _caseless_get(item, "allowoverlay", True))),
            open_vr=int(_caseless_get(item, "OpenVR", _caseless_get(item, "openvr", 0)) or 0),
            devkit=int(_caseless_get(item, "Devkit", _caseless_get(item, "devkit", 0)) or 0),
            devkit_game_id=str(_caseless_get(item, "DevkitGameID", _caseless_get(item, "devkitgameid", "")) or ""),
            devkit_override_appid=int(_caseless_get(item, "DevkitOverrideAppID", _caseless_get(item, "devkitoverrideappid", 0)) or 0),
            last_play_time=int(_caseless_get(item, "LastPlayTime", _caseless_get(item, "lastplaytime", 0)) or 0),
            tags=[str(x) for x in (_caseless_get(item, "tags", []) or [])],
            flatpak_app_id=str(_caseless_get(item, "FlatpakAppID", _caseless_get(item, "flatpakappid", "")) or ""),
            kv=item,
        )
        out.append(sc)
    return out


def write_shortcuts_vdf(shortcuts: List[Shortcut]) -> bytes:
    # IMPORTANT: keep original KV dict object when possible to preserve insertion order.
    sc_objs: List[Dict[str, Any]] = []
    for sc in shortcuts:
        kvobj = sc.kv if isinstance(sc.kv, dict) else {}

        _caseless_set(kvobj, "appid", int(sc.appid))
        _caseless_set(kvobj, "AppName", sc.app_name)
        _caseless_set(kvobj, "exe", sc.exe)
        _caseless_set(kvobj, "StartDir", sc.start_dir)
        _caseless_set(kvobj, "icon", sc.icon)
        _caseless_set(kvobj, "ShortcutPath", sc.shortcut_path)
        _caseless_set(kvobj, "LaunchOptions", sc.launch_options)
        _caseless_set(kvobj, "IsHidden", bool(sc.is_hidden))
        _caseless_set(kvobj, "AllowDesktopConfig", bool(sc.allow_desktop_config))
        _caseless_set(kvobj, "AllowOverlay", bool(sc.allow_overlay))

        # Preserve type (SRM/steam-shortcut-editor stores OpenVR as bool in many fixtures)
        if isinstance(_caseless_get(kvobj, "OpenVR", 0), bool):
            _caseless_set(kvobj, "OpenVR", bool(sc.open_vr))
        else:
            _caseless_set(kvobj, "OpenVR", int(sc.open_vr))

        _caseless_set(kvobj, "Devkit", int(sc.devkit))
        _caseless_set(kvobj, "DevkitGameID", sc.devkit_game_id)
        _caseless_set(kvobj, "DevkitOverrideAppID", int(sc.devkit_override_appid))
        _caseless_set(kvobj, "LastPlayTime", int(sc.last_play_time))

        if sc.flatpak_app_id:
            _caseless_set(kvobj, "FlatpakAppID", sc.flatpak_app_id)

        _caseless_set(kvobj, "tags", list(sc.tags))

        sc_objs.append(kvobj)

    root = {"shortcuts": sc_objs}
    return kv_write(root)


def _shortcut_key_srm(exe: str, app_name: str, launch_options: str) -> str:
    """
    SRM uses generateAppId(exe, appname) as its stable key for indexing.
    We'll approximate by using the same 'long id' decimal string.
    """
    top32 = shortcut_appid(exe, app_name) & 0xFFFFFFFF
    long_id = shortcut_long_appid(top32)
    return f"{int(long_id)}|{launch_options or ''}"


def _verify_shortcuts_vdf_roundtrip(original: bytes) -> Tuple[int, bool]:
    """
    More SRM-like: verify semantic stability (parse->write->parse) instead of byte identity.
    This avoids rejecting real-world shortcuts.vdf that serialize with different ordering.
    """
    try:
        a = parse_shortcuts_vdf(original)
    except Exception:
        # If we can't parse, it's not valid for us anyway.
        return 0, False

    rebuilt = write_shortcuts_vdf(a)

    try:
        b = parse_shortcuts_vdf(rebuilt)
    except Exception:
        return len(a), False

    def norm(sc: Shortcut) -> Dict[str, Any]:
        return {
            "key": _shortcut_key_srm(sc.exe, sc.app_name, sc.launch_options),
            "appid": int(sc.appid),
            "app_name": sc.app_name,
            "exe": sc.exe,
            "start_dir": sc.start_dir,
            "icon": sc.icon,
            "shortcut_path": sc.shortcut_path,
            "launch_options": sc.launch_options,
            "is_hidden": bool(sc.is_hidden),
            "allow_desktop_config": bool(sc.allow_desktop_config),
            "allow_overlay": bool(sc.allow_overlay),
            "open_vr": int(sc.open_vr),
            "devkit": int(sc.devkit),
            "devkit_game_id": sc.devkit_game_id,
            "devkit_override_appid": int(sc.devkit_override_appid),
            "last_play_time": int(sc.last_play_time),
            "tags": list(sc.tags),
            "flatpak_app_id": sc.flatpak_app_id,
        }

    ma = {norm(x)["key"]: norm(x) for x in a}
    mb = {norm(x)["key"]: norm(x) for x in b}

    return len(a), (ma == mb)


# ----------------------------
# Controller template (Steam Deck / neptune)
# ----------------------------
DEFAULT_NEPTUNE_TEMPLATE = "controller_neptune_gamepad+mouse.vdf"


def transform_title(game_title: str) -> str:
    s = (game_title or "").lower()
    bad = set('/\\?%*:|"<>.')
    return "".join(ch for ch in s if ch not in bad)


def _resolve_neptune_profile(steam_root: Path, template_or_workshop: str) -> Tuple[str, str]:
    v = (template_or_workshop or "").strip()
    if not v:
        return ("template", DEFAULT_NEPTUNE_TEMPLATE)

    valve = steam_root / "controller_base" / "templates" / v
    if valve.exists():
        return ("template", v)

    workshop = steam_root / "steamapps" / "workshop" / "content" / "241100" / v
    if workshop.is_dir():
        return ("workshop", v)

    raise FileNotFoundError(
        f"Template not found as Valve template ({valve}) or Workshop folder ({workshop})."
    )


def _vdf_tokenize(text: str) -> List[str]:
    toks: List[str] = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
            continue
        if c == '"':
            i += 1
            out = []
            while i < n:
                c2 = text[i]
                if c2 == '"':
                    i += 1
                    break
                out.append(c2)
                i += 1
            toks.append("".join(out))
            continue
        if c == '{' or c == '}':
            toks.append(c)
            i += 1
            continue
        j = i
        while j < n and (not text[j].isspace()) and text[j] not in "{}":
            j += 1
        toks.append(text[i:j])
        i = j
    return toks


def _parse_configset_controller_vdf(text: str) -> Dict[str, Dict[str, str]]:
    toks = _vdf_tokenize(text)
    i = 0

    def expect(tok: str) -> None:
        nonlocal i
        if i >= len(toks) or toks[i] != tok:
            got = toks[i] if i < len(toks) else "<eof>"
            raise ValueError(f"VDF parse error: expected {tok!r}, got {got!r}")
        i += 1

    def parse_kv_block() -> Dict[str, str]:
        nonlocal i
        d: Dict[str, str] = {}
        expect("{")
        while i < len(toks) and toks[i] != "}":
            key = toks[i]
            i += 1
            val = toks[i]
            i += 1
            d[str(key)] = str(val)
        expect("}")
        return d

    while i < len(toks):
        if toks[i] == "controller_config":
            i += 1
            expect("{")
            entries: Dict[str, Dict[str, str]] = {}
            while i < len(toks) and toks[i] != "}":
                entry_key = toks[i]
                i += 1
                entry_vals = parse_kv_block()
                entries[str(entry_key)] = entry_vals
            expect("}")
            return entries
        i += 1

    return {}


def _write_configset_controller_vdf(entries: Dict[str, Dict[str, str]]) -> str:
    lines: List[str] = []
    lines.append('"controller_config"')
    lines.append("{")
    for k in sorted(entries.keys(), key=lambda x: x):
        lines.append(f'\t"{k}"')
        lines.append("\t{")
        inner = entries[k]
        preferred = ["template", "workshop", "autosave", "srmAppId", "srmParserId"]
        inner_keys: List[str] = []
        for p in preferred:
            if p in inner:
                inner_keys.append(p)
        for kk in sorted(inner.keys()):
            if kk not in inner_keys:
                inner_keys.append(kk)
        for kk in inner_keys:
            vv = inner[kk]
            lines.append(f'\t\t"{kk}"\t\t"{vv}"')
        lines.append("\t}")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def _verify_configset_roundtrip(text: str) -> bool:
    entries = _parse_configset_controller_vdf(text)
    out = _write_configset_controller_vdf(entries)
    entries2 = _parse_configset_controller_vdf(out)
    return entries == entries2


def apply_neptune_template(
        steam_root: Path,
        steamid: str,
        new_title: str,
        old_title: Optional[str],
        template_filename: str,
        force: bool,
        flatpak_app_id: Optional[str] = None,
        sspy_app_id: Optional[str] = None,
        sspy_parser_id: Optional[str] = None,
) -> Tuple[str, str]:
    profile_type, mapping_id = _resolve_neptune_profile(steam_root, template_filename)

    ctrl_id = controller_config_id(steamid)
    configset_dir = steam_root / "steamapps" / "common" / "Steam Controller Configs" / ctrl_id / "config"
    configset_dir.mkdir(parents=True, exist_ok=True)
    configset_path = configset_dir / "configset_controller_neptune.vdf"

    entries: Dict[str, Dict[str, str]] = {}
    created = False
    if configset_path.exists():
        text = configset_path.read_text("utf-8", errors="replace")
        if not _verify_configset_roundtrip(text):
            raise ValueError("configset_controller_neptune.vdf round-trip verification failed")
        entries = _parse_configset_controller_vdf(text)
    else:
        created = True

    new_key = transform_title(new_title)
    old_key = transform_title(old_title) if old_title is not None else None
    alt_key = transform_title(flatpak_app_id) if flatpak_app_id else None

    action = "no-op"

    # Remove old key only if it looks like one we own (srmAppId) OR a plain template/workshop-only entry.
    removed_old = False
    if old_key and old_key != new_key:
        old_entry = entries.get(old_key) or {}
        if ("srmAppId" in old_entry) or (("template" in old_entry or "workshop" in old_entry) and len(old_entry.keys()) == 1):
            del entries[old_key]
            removed_old = True

    def make_entry() -> Dict[str, str]:
        e: Dict[str, str] = {profile_type: str(mapping_id)}
        if sspy_app_id:
            e["srmAppId"] = str(sspy_app_id)
        if sspy_parser_id:
            e["srmParserId"] = str(sspy_parser_id)
        return e

    # Canonical key selection:
    # Prefer the title-derived key (Steam usually uses this for non-Steam shortcuts).
    # If it doesn't exist but the flatpak key does, update the flatpak key.
    if new_key in entries:
        key_to_use = new_key
    elif alt_key and alt_key in entries:
        key_to_use = alt_key
    else:
        key_to_use = new_key  # create the canonical one

    if key_to_use in entries:
        if force:
            entries[key_to_use] = make_entry()
            action = "forced"
        else:
            action = "skipped-existing"
    else:
        entries[key_to_use] = make_entry()
        action = "created-file" if created else "applied"

    # If both keys exist, prefer keeping the canonical (title) key.
    # Remove the flatpak-style key only if it looks like one we own.
    removed_alt = False
    if alt_key and (alt_key != key_to_use) and (alt_key in entries):
        alt_entry = entries.get(alt_key) or {}
        if ("srmAppId" in alt_entry) or (("template" in alt_entry or "workshop" in alt_entry) and len(alt_entry.keys()) == 1):
            del entries[alt_key]
            removed_alt = True

    if (removed_old or removed_alt) and action in ("no-op", "skipped-existing"):
        action = "removed-old-key"

    if configset_path.exists():
        bk = configset_path.with_suffix(".vdf.backup")
        shutil.copyfile(configset_path, bk)

    out_text = _write_configset_controller_vdf(entries)
    configset_path.write_text(out_text, "utf-8")

    return action, str(configset_path)


# ----------------------------
# Artwork install
# ----------------------------
def install_artwork(
        grid_dir: Path,
        appid: int,
        grid: Optional[Path] = None,
        hero: Optional[Path] = None,
        logo: Optional[Path] = None,
        portrait: Optional[Path] = None
) -> None:
    grid_dir.mkdir(parents=True, exist_ok=True)

    appid32 = int(appid) & 0xFFFFFFFF
    long_id = shortcut_long_appid(appid32)

    def copy_both(src: Path, dst32: Path, dst64: Path) -> None:
        shutil.copyfile(src, dst32)
        shutil.copyfile(src, dst64)

    if grid:
        copy_both(grid, grid_dir / f"{appid32}.png", grid_dir / f"{long_id}.png")
    if portrait:
        copy_both(portrait, grid_dir / f"{appid32}p.png", grid_dir / f"{long_id}p.png")
    if hero:
        copy_both(hero, grid_dir / f"{appid32}_hero.png", grid_dir / f"{long_id}_hero.png")
    if logo:
        copy_both(logo, grid_dir / f"{appid32}_logo.png", grid_dir / f"{long_id}_logo.png")

# ----------------------------
# localconfig.vdf (Steam Input enable/disable/default)
# ----------------------------
LOCAL_TOP_KEY = "UserLocalConfigStore"


def _vdf_parse_generic(text: str) -> Dict[str, Any]:
    toks = _vdf_tokenize(text)
    i = 0

    def parse_object() -> Dict[str, Any]:
        nonlocal i
        d: Dict[str, Any] = {}
        while i < len(toks):
            tok = toks[i]
            if tok == "}":
                return d
            key = tok
            i += 1
            if i >= len(toks):
                d[str(key)] = ""
                return d
            if toks[i] == "{":
                i += 1
                child = parse_object()
                if i < len(toks) and toks[i] == "}":
                    i += 1
                d[str(key)] = child
            else:
                val = toks[i]
                i += 1
                d[str(key)] = str(val)
        return d

    root: Dict[str, Any] = {}
    while i < len(toks):
        key = toks[i]
        i += 1
        if i < len(toks) and toks[i] == "{":
            i += 1
            obj = parse_object()
            if i < len(toks) and toks[i] == "}":
                i += 1
            root[str(key)] = obj
        else:
            if i < len(toks):
                root[str(key)] = str(toks[i])
                i += 1
            else:
                root[str(key)] = ""
    return root


def _vdf_write_generic(root: Dict[str, Any], indent: str = "") -> str:
    lines: List[str] = []

    def write_kv(k: str, v: str, ind: str) -> None:
        lines.append(f'{ind}"{k}"\t\t"{v}"')

    def write_obj(obj: Dict[str, Any], ind: str) -> None:
        for k in sorted(obj.keys(), key=lambda x: x):
            v = obj[k]
            if isinstance(v, dict):
                lines.append(f'{ind}"{k}"')
                lines.append(f"{ind}" + "{")
                write_obj(v, ind + "\t")
                lines.append(f"{ind}" + "}")
            else:
                write_kv(str(k), str(v), ind)

    write_obj(root, indent)
    lines.append("")
    return "\n".join(lines)


def _localconfig_path(steam_root: Path, steamid: str) -> Path:
    return steam_root / "userdata" / steamid / "config" / "localconfig.vdf"


def _shortcutify_appid_for_localconfig(appid32: int) -> str:
    return str(int(appid32) & 0xFFFFFFFF)


def _apply_steam_input_setting(
        steam_root: Path,
        steamid: str,
        appid32: int,
        steam_input: str,
        sspy_parser_id: str,
        verify_only: bool,
) -> Tuple[bool, str]:
    lc_path = _localconfig_path(steam_root, steamid)
    lc_path.parent.mkdir(parents=True, exist_ok=True)

    if lc_path.exists():
        txt = lc_path.read_text("utf-8", errors="replace")
        try:
            data: Dict[str, Any] = _vdf_parse_generic(txt)
        except Exception:
            data = {}
    else:
        data = {}

    if LOCAL_TOP_KEY not in data or not isinstance(data.get(LOCAL_TOP_KEY), dict):
        data[LOCAL_TOP_KEY] = {}
    u = data[LOCAL_TOP_KEY]
    if "apps" not in u or not isinstance(u.get("apps"), dict):
        u["apps"] = {}

    apps = u["apps"]
    shift_id = _shortcutify_appid_for_localconfig(appid32)

    changed = False

    if steam_input == "1":
        cur = apps.get(shift_id)
        if isinstance(cur, dict) and ("srmParserId" in cur):
            del apps[shift_id]
            changed = True
    else:
        apps.setdefault(shift_id, {})
        if not isinstance(apps[shift_id], dict):
            apps[shift_id] = {}
        entry = apps[shift_id]

        if str(entry.get("SteamControllerRumble")) != "-1":
            entry["SteamControllerRumble"] = -1
            changed = True
        if str(entry.get("SteamControllerRumbleIntensity")) != "320":
            entry["SteamControllerRumbleIntensity"] = 320
            changed = True
        if str(entry.get("UseSteamControllerConfig")) != str(steam_input):
            entry["UseSteamControllerConfig"] = str(steam_input)
            changed = True
        if str(entry.get("srmParserId")) != ("p" + str(sspy_parser_id)):
            entry["srmParserId"] = "p" + str(sspy_parser_id)
            changed = True

    if verify_only:
        return changed, f"localconfig.vdf: {lc_path} (would_change={changed})"

    if changed:
        if lc_path.exists():
            shutil.copyfile(lc_path, lc_path.with_suffix(".vdf.backup"))
        out = _vdf_write_generic(data)
        lc_path.write_text(out, "utf-8")
        return True, f"localconfig.vdf updated: {lc_path}"

    return False, f"localconfig.vdf unchanged: {lc_path}"


# ----------------------------
# Dump helpers
# ----------------------------
def _raw_scan_appnames(data: bytes) -> List[str]:
    # KV-format: best-effort parse, then pull AppName/appname.
    out: List[str] = []
    try:
        root = kv_parse(data, auto_arrays=True, auto_bools=True)
        sc = root.get("shortcuts", []) if isinstance(root, dict) else []
        if isinstance(sc, list):
            for it in sc:
                if isinstance(it, dict):
                    nm = _caseless_get(it, "AppName", _caseless_get(it, "appname", ""))
                    if nm:
                        out.append(str(nm))
    except Exception:
        pass

    seen = set()
    uniq: List[str] = []
    for s in out:
        if s not in seen:
            seen.add(s)
            uniq.append(s)
    return uniq


def _dump_artwork_file_info(grid_dir: Path, appid32: int, long_id: int, suffix: str) -> Dict[str, object]:
    """
    Steam Deck UI commonly uses the long 64-bit id in filenames. We check both:
      <long_id><suffix>.png and <appid32><suffix>.png
    """
    p64 = grid_dir / f"{long_id}{suffix}.png"
    p32 = grid_dir / f"{appid32}{suffix}.png"

    chosen = None
    if p64.exists():
        chosen = p64
    elif p32.exists():
        chosen = p32

    if chosen is not None and chosen.exists():
        st = chosen.stat()
        return {
            "path": str(chosen),
            "exists": True,
            "size": int(st.st_size),
            "mtime": float(st.st_mtime),
            "candidates": [str(p64), str(p32)],
            "matched": "long_id" if chosen == p64 else "appid32",
        }

    return {
        "path": str(p64),  # prefer long-id path for readability
        "exists": False,
        "candidates": [str(p64), str(p32)],
        "matched": None,
    }


def _dump_json(
        steam_root: Path,
        steamid: str,
        shortcuts_vdf: Path,
        grid_dir: Path,
        dump_name: Optional[str],
        dump_appid: Optional[int],
        dump_list: bool,
) -> int:
    ctrl_id = controller_config_id(steamid)
    configset_path = steam_root / "steamapps" / "common" / "Steam Controller Configs" / ctrl_id / "config" / "configset_controller_neptune.vdf"

    result: Dict[str, object] = {
        "steam_root": str(steam_root),
        "steamid": str(steamid),
        "controller_id": str(ctrl_id),
        "paths": {
            "shortcuts_vdf": str(shortcuts_vdf),
            "grid_dir": str(grid_dir),
            "configset_neptune": str(configset_path),
            "templates_dir": str(steam_root / "controller_base" / "templates"),
            "localconfig_vdf": str(_localconfig_path(steam_root, steamid)),
        },
        "matches": [],
        "list": {"source": None, "items": []},
        "verification": {
            "shortcuts_vdf": {"present": shortcuts_vdf.exists(), "roundtrip_ok": None, "count": 0, "error": None},
            "configset_neptune": {"present": configset_path.exists(), "roundtrip_ok": None, "error": None},
            "localconfig_vdf": {"present": _localconfig_path(steam_root, steamid).exists(), "error": None},
        },
        "raw_scan": {"app_names": []},
    }

    configset_entries: Dict[str, Dict[str, str]] = {}
    if configset_path.exists():
        try:
            txt = configset_path.read_text("utf-8", errors="replace")
            ok = _verify_configset_roundtrip(txt)
            result["verification"]["configset_neptune"]["roundtrip_ok"] = bool(ok)
            if ok:
                configset_entries = _parse_configset_controller_vdf(txt)
        except Exception as e:
            result["verification"]["configset_neptune"]["roundtrip_ok"] = False
            result["verification"]["configset_neptune"]["error"] = str(e)

    shortcuts: List[Shortcut] = []
    if shortcuts_vdf.exists():
        data = shortcuts_vdf.read_bytes()
        try:
            result["raw_scan"]["app_names"] = _raw_scan_appnames(data)
        except Exception:
            result["raw_scan"]["app_names"] = []

        try:
            count, ok = _verify_shortcuts_vdf_roundtrip(data)
            result["verification"]["shortcuts_vdf"]["roundtrip_ok"] = bool(ok)
            result["verification"]["shortcuts_vdf"]["count"] = int(count)
            if ok:
                shortcuts = parse_shortcuts_vdf(data)
        except Exception as e:
            result["verification"]["shortcuts_vdf"]["roundtrip_ok"] = False
            result["verification"]["shortcuts_vdf"]["error"] = str(e)

    if dump_list:
        if shortcuts:
            result["list"]["source"] = "parsed"
            result["list"]["items"] = [{
                "app_name": s.app_name,
                # shortcuts.vdf appid is signed int32, show both views for clarity
                "appid_signed": {"dec": int(s.appid), "hex": f"0x{(int(s.appid) & 0xFFFFFFFF):08x}"},
                "appid32_bits": {"dec": (int(s.appid) & 0xFFFFFFFF), "hex": f"0x{(int(s.appid) & 0xFFFFFFFF):08x}"},
                "exe": s.exe,
                "flatpak_app_id": s.flatpak_app_id,
            } for s in shortcuts]
        else:
            result["list"]["source"] = "raw_scan"
            result["list"]["items"] = [{"app_name": x} for x in result["raw_scan"]["app_names"]]

    matches: List[Shortcut] = []
    selection = {"by": None, "value": None}
    if shortcuts:
        if dump_appid is not None:
            selection = {"by": "appid", "value": dump_appid}
            matches = [s for s in shortcuts if int(s.appid) == int(dump_appid)]
        elif dump_name is not None:
            selection = {"by": "name", "value": dump_name}
            matches = [s for s in shortcuts if s.app_name == dump_name]
            if not matches:
                dn = dump_name.casefold()
                matches = [s for s in shortcuts if s.app_name.casefold() == dn]

    for s in matches:
        # shortcuts.vdf stores signed int32; artwork/controller keys use the unsigned "top32" bits.
        stored_appid_signed = int(s.appid)
        stored_appid32 = stored_appid_signed & 0xFFFFFFFF

        computed_appid32 = int(shortcut_appid(s.exe, s.app_name)) & 0xFFFFFFFF
        computed_signed = int(shortcut_appid_signed(s.exe, s.app_name))

        long_id = shortcut_long_appid(stored_appid32)
        title_key = transform_title(s.app_name)

        keys_tried = [title_key]
        if s.flatpak_app_id:
            keys_tried.append(transform_title(s.flatpak_app_id))
        keys_tried.extend([s.app_name.casefold(), str(stored_appid32)])

        found_key = None
        entry = None
        for k in keys_tried:
            if k in configset_entries:
                found_key = k
                entry = configset_entries.get(k)
                break

        result["matches"].append({
            "selection": selection,
            "shortcut": {
                "app_name": s.app_name,
                "appid_signed": {"dec": stored_appid_signed, "hex": f"0x{stored_appid32:08x}"},
                "appid32_bits": {"dec": stored_appid32, "hex": f"0x{stored_appid32:08x}"},
                "long_appid": {"dec": int(long_id), "hex": f"0x{int(long_id):016x}"},
                "computed_appid32": {"dec": computed_appid32, "hex": f"0x{computed_appid32:08x}", "matches_stored_bits": (computed_appid32 == stored_appid32)},
                "computed_appid_signed": {"dec": computed_signed, "hex": f"0x{(computed_signed & 0xFFFFFFFF):08x}", "matches_stored": (computed_signed == stored_appid_signed)},
                "exe": s.exe,
                "start_dir": s.start_dir,
                "launch_options": s.launch_options,
                "shortcut_path": s.shortcut_path,
                "icon": s.icon,
                "flatpak_app_id": s.flatpak_app_id,
                "tags": list(s.tags),
            },
            "controller": {
                "title_key": title_key,
                "flatpak_key": transform_title(s.flatpak_app_id) if s.flatpak_app_id else None,
                "configset_present": bool(configset_path.exists()),
                "entry_present": bool(entry is not None),
                "found_key": found_key,
                "keys_tried": keys_tried,
                "entry": entry if entry is not None else None,
            },
            "artwork": {
                "grid_dir": str(grid_dir),
                "files": {
                    # artwork filenames are based on the unsigned 32-bit bits + long_id
                    "grid": _dump_artwork_file_info(grid_dir, stored_appid32, long_id, ""),
                    "portrait": _dump_artwork_file_info(grid_dir, stored_appid32, long_id, "p"),
                    "hero": _dump_artwork_file_info(grid_dir, stored_appid32, long_id, "_hero"),
                    "logo": _dump_artwork_file_info(grid_dir, stored_appid32, long_id, "_logo"),
                },
            },
        })

    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


# ----------------------------
# Main action helper (single steamid)
# ----------------------------
def _apply_for_steamid(steam_root: Path, steamid: str, args: argparse.Namespace) -> Tuple[int, str]:
    userdir = steam_root / "userdata" / steamid
    cfg = userdir / "config"
    vdf_path = cfg / "shortcuts.vdf"
    cfg.mkdir(parents=True, exist_ok=True)

    exe = args.exe
    name = args.name
    appid_signed = shortcut_appid_signed(exe, name)
    appid32 = shortcut_appid(exe, name) & 0xFFFFFFFF
    long_id = shortcut_long_appid(appid32)

    tags = [t.strip() for t in (args.tags or "").split(",") if t.strip()]

    flatpak_app_id = ""
    if args.flatpak_app_id:
        flatpak_app_id = args.flatpak_app_id
    else:
        toks = [t.strip() for t in (args.launch or "").replace('"', "").split() if t.strip()]
        for t in reversed(toks):
            if t.startswith(("io.", "com.", "org.")) and "." in t:
                flatpak_app_id = t
                break

    grid_dir = cfg / "grid"
    grid_dir.mkdir(parents=True, exist_ok=True)

    # Optional: copy icon into grid dir and set shortcut icon path
    if args.icon_file:
        src_icon = Path(args.icon_file)
        if not src_icon.exists():
            return 2, f"icon-file not found: {src_icon}"

        # If you want EXACTLY io.github.sookyboo.nox-decomp.png:
        if flatpak_app_id:
            dst_icon = grid_dir / f"{flatpak_app_id}.png"
        else:
            dst_icon = grid_dir / f"{transform_title(name)}.png"

        shutil.copyfile(src_icon, dst_icon)
        args.icon = str(dst_icon)

    sc_new = Shortcut(
        appid=int(appid_signed),
        app_name=name,
        exe=exe,
        start_dir=args.startdir,
        icon=args.icon,
        shortcut_path=args.shortcutpath,
        launch_options=args.launch,
        is_hidden=bool(args.hidden),
        allow_overlay=not args.no_overlay,
        allow_desktop_config=not args.no_desktop_config,
        tags=tags,
        last_play_time=0,
        flatpak_app_id=flatpak_app_id,
        kv={},  # filled if we match an existing entry
    )

    existing: List[Shortcut] = []
    if vdf_path.exists():
        data = vdf_path.read_bytes()
        try:
            count, ok = _verify_shortcuts_vdf_roundtrip(data)
        except Exception as e:
            return 4, f"shortcuts.vdf verification failed for {steamid}: {e}"
        if not ok:
            return 4, f"shortcuts.vdf verification failed for {steamid}: round-trip mismatch ({count} shortcuts)"
        existing = parse_shortcuts_vdf(data)

    updated = False
    old_name: Optional[str] = None

    # Prefer FlatpakAppID matching (stable across renames)
    if sc_new.flatpak_app_id:
        for idx, sc in enumerate(existing):
            if sc.flatpak_app_id and sc.flatpak_app_id == sc_new.flatpak_app_id:
                old_name = sc.app_name
                sc_new.kv = sc.kv  # preserve unknown fields + insertion order
                existing[idx] = sc_new
                updated = True
                break

    # Then match by signed appid (shortcuts.vdf stores signed int32)
    if not updated:
        for idx, sc in enumerate(existing):
            if int(sc.appid) == int(sc_new.appid):
                old_name = sc.app_name
                sc_new.kv = sc.kv
                if not sc_new.flatpak_app_id and sc.flatpak_app_id:
                    sc_new.flatpak_app_id = sc.flatpak_app_id
                existing[idx] = sc_new
                updated = True
                break

    # Then match by exe+launch_options
    if not updated:
        for idx, sc in enumerate(existing):
            if sc.exe == sc_new.exe and sc.launch_options == sc_new.launch_options and sc.exe:
                old_name = sc.app_name
                sc_new.kv = sc.kv
                if not sc_new.flatpak_app_id and sc.flatpak_app_id:
                    sc_new.flatpak_app_id = sc.flatpak_app_id
                existing[idx] = sc_new
                updated = True
                break

    if not updated:
        existing.append(sc_new)

    out_bytes = write_shortcuts_vdf(existing)

    try:
        _verify_shortcuts_vdf_roundtrip(out_bytes)
    except Exception as e:
        return 5, f"generated shortcuts.vdf verification failed for {steamid}: {e}"

    # Controller template preflight
    if args.template is not None:
        try:
            templates_dir = steam_root / "controller_base" / "templates"
            template_path = templates_dir / args.template
            if not template_path.exists():
                raise FileNotFoundError(f"Template not found: {template_path}")

            ctrl_id = controller_config_id(steamid)
            configset_dir = steam_root / "steamapps" / "common" / "Steam Controller Configs" / ctrl_id / "config"
            configset_path = configset_dir / "configset_controller_neptune.vdf"
            if configset_path.exists():
                txt = configset_path.read_text("utf-8", errors="replace")
                if not _verify_configset_roundtrip(txt):
                    raise ValueError("configset_controller_neptune.vdf round-trip verification failed")
        except Exception as e:
            return 6, f"Controller template verification failed for {steamid}: {e}"

    # localconfig preflight (parse)
    if args.steam_input is not None:
        try:
            lc = _localconfig_path(steam_root, steamid)
            if lc.exists():
                _vdf_parse_generic(lc.read_text("utf-8", errors="replace"))
        except Exception as e:
            return 6, f"localconfig.vdf verification failed for {steamid}: {e}"

    if args.verify_only:
        msgs = [f"Verification OK for {steamid}."]
        if args.steam_input is not None:
            _chg, _m = _apply_steam_input_setting(
                steam_root=steam_root,
                steamid=steamid,
                appid32=appid32,
                steam_input=str(args.steam_input),
                sspy_parser_id=str(args.sspy_parser_id),
                verify_only=True,
            )
            msgs.append(_m)
        return 0, "\n".join(msgs)

    if vdf_path.exists():
        bak = vdf_path.with_suffix(".vdf.bak")
        shutil.copyfile(vdf_path, bak)

    vdf_path.write_bytes(out_bytes)

    grid_dir = cfg / "grid"
    if args.grid or args.hero or args.logo or args.portrait:
        install_artwork(
            grid_dir=grid_dir,
            appid=appid32,
            grid=Path(args.grid) if args.grid else None,
            hero=Path(args.hero) if args.hero else None,
            logo=Path(args.logo) if args.logo else None,
            portrait=Path(args.portrait) if args.portrait else None,
        )

    # Steam Input (localconfig.vdf)
    localconfig_msg = None
    if args.steam_input is not None:
        _changed, localconfig_msg = _apply_steam_input_setting(
            steam_root=steam_root,
            steamid=steamid,
            appid32=appid32,
            steam_input=str(args.steam_input),
            sspy_parser_id=str(args.sspy_parser_id),
            verify_only=False,
        )

    controller_action = None
    controller_path = None
    if args.template is not None:
        try:
            controller_action, controller_path = apply_neptune_template(
                steam_root=steam_root,
                steamid=steamid,
                new_title=name,
                old_title=old_name,
                template_filename=args.template,
                force=bool(args.force_template),
                flatpak_app_id=sc_new.flatpak_app_id if sc_new.flatpak_app_id else None,
                sspy_app_id="a" + str(long_id),
                sspy_parser_id="p" + str(args.sspy_parser_id),
            )
        except Exception as e:
            return 3, f"Controller template error for {steamid}: {e}"

    action = "Updated" if updated else "Added"
    msg = []
    msg.append(f"{action} shortcut for SteamID {steamid}")
    msg.append(f"shortcuts.vdf: {vdf_path}")
    msg.append(f"appid: {appid32} (0x{appid32:08x})")
    msg.append(f"long_appid: {long_id}")
    if sc_new.flatpak_app_id:
        msg.append(f"flatpak_app_id: {sc_new.flatpak_app_id}")
    if args.grid or args.hero or args.logo or args.portrait:
        msg.append(f"artwork dir: {grid_dir}")
    if localconfig_msg is not None:
        msg.append(localconfig_msg)
    if controller_action is not None:
        msg.append(f"controller template: {args.template}")
        msg.append(f"controller action: {controller_action}")
        msg.append(f"controller configset: {controller_path}")
        msg.append(f"controller key: {transform_title(name)}")
        msg.append(f"sspyAppId: a{long_id}")
        msg.append(f"sspyParserId: p{args.sspy_parser_id}")
    return 0, "\n".join(msg)

# ----------------------------
# Shortcut removal
# ----------------------------
def _unlink_if_exists(path: Path) -> None:
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def _remove_shortcut_artwork(grid_dir: Path, sc: Shortcut, flatpak_app_id: str) -> None:
    appid32 = int(sc.appid) & 0xFFFFFFFF
    long_id = shortcut_long_appid(appid32)
    for suffix in ("", "p", "_hero", "_logo"):
        _unlink_if_exists(grid_dir / f"{appid32}{suffix}.png")
        _unlink_if_exists(grid_dir / f"{long_id}{suffix}.png")
    if flatpak_app_id:
        _unlink_if_exists(grid_dir / f"{flatpak_app_id}.png")


def _remove_controller_entries(steam_root: Path, steamid: str, removed: List[Shortcut], flatpak_app_id: str) -> None:
    ctrl_id = controller_config_id(steamid)
    configset_path = steam_root / "steamapps" / "common" / "Steam Controller Configs" / ctrl_id / "config" / "configset_controller_neptune.vdf"
    if not configset_path.exists():
        return

    text = configset_path.read_text("utf-8", errors="replace")
    if not _verify_configset_roundtrip(text):
        raise ValueError("configset_controller_neptune.vdf round-trip verification failed")

    entries = _parse_configset_controller_vdf(text)
    keys = {transform_title(flatpak_app_id)} if flatpak_app_id else set()
    owned_appids = set()
    for sc in removed:
        keys.add(transform_title(sc.app_name))
        appid32 = int(sc.appid) & 0xFFFFFFFF
        owned_appids.add("a" + str(shortcut_long_appid(appid32)))

    changed = False
    for key in list(entries.keys()):
        entry = entries.get(key) or {}
        if key in keys or str(entry.get("srmAppId", "")) in owned_appids:
            del entries[key]
            changed = True

    if changed:
        shutil.copyfile(configset_path, configset_path.with_suffix(".vdf.backup"))
        configset_path.write_text(_write_configset_controller_vdf(entries), "utf-8")


def _remove_localconfig_entries(steam_root: Path, steamid: str, removed: List[Shortcut]) -> None:
    lc_path = _localconfig_path(steam_root, steamid)
    if not lc_path.exists():
        return

    data = _vdf_parse_generic(lc_path.read_text("utf-8", errors="replace"))
    store = data.get(LOCAL_TOP_KEY)
    if not isinstance(store, dict):
        return
    apps = store.get("apps")
    if not isinstance(apps, dict):
        return

    changed = False
    for sc in removed:
        key = _shortcutify_appid_for_localconfig(int(sc.appid) & 0xFFFFFFFF)
        entry = apps.get(key)
        if isinstance(entry, dict) and "srmParserId" in entry:
            del apps[key]
            changed = True

    if changed:
        shutil.copyfile(lc_path, lc_path.with_suffix(".vdf.backup"))
        lc_path.write_text(_vdf_write_generic(data), "utf-8")


def remove_flatpak_shortcut_for_steamid(steam_root: Path, steamid: str, flatpak_app_id: str) -> Tuple[int, str]:
    cfg = steam_root / "userdata" / steamid / "config"
    vdf_path = cfg / "shortcuts.vdf"
    if not vdf_path.exists():
        return 0, f"SteamID {steamid}: no shortcuts.vdf"

    data = vdf_path.read_bytes()
    _count, ok = _verify_shortcuts_vdf_roundtrip(data)
    if not ok:
        return 4, f"SteamID {steamid}: shortcuts.vdf verification failed"

    shortcuts = parse_shortcuts_vdf(data)
    removed = [sc for sc in shortcuts if (sc.flatpak_app_id or "") == flatpak_app_id]
    if not removed:
        return 0, f"SteamID {steamid}: OpenRealm shortcut not present"

    kept = [sc for sc in shortcuts if (sc.flatpak_app_id or "") != flatpak_app_id]
    vdf_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(vdf_path, vdf_path.with_suffix(".vdf.backup"))
    vdf_path.write_bytes(write_shortcuts_vdf(kept))

    grid_dir = cfg / "grid"
    for sc in removed:
        _remove_shortcut_artwork(grid_dir, sc, flatpak_app_id)

    try:
        _remove_controller_entries(steam_root, steamid, removed, flatpak_app_id)
    except Exception as e:
        return 5, f"SteamID {steamid}: shortcut removed, controller cleanup failed: {e}"

    try:
        _remove_localconfig_entries(steam_root, steamid, removed)
    except Exception as e:
        return 6, f"SteamID {steamid}: shortcut removed, localconfig cleanup failed: {e}"

    return 0, f"SteamID {steamid}: removed {len(removed)} shortcut(s) for {flatpak_app_id}"


# ----------------------------
# Main
# ----------------------------
def main() -> int:
    ap = argparse.ArgumentParser(
        description="Add, update, inspect, or remove Steam shortcuts.vdf entries and artwork. No dependencies."
    )
    ap.add_argument("--steam-root", type=str, default=None, help="Path to Steam root (auto-detect if omitted)")
    ap.add_argument("--steamid", type=str, default=None, help="SteamID folder under userdata/ (auto-pick if omitted)")
    ap.add_argument("--all-steamids", action="store_true", help="Apply the requested write operation to all numeric userdata SteamID folders.")
    ap.add_argument("--remove-flatpak-app-id", type=str, default=None, help="Remove shortcuts matching this FlatpakAppID, including installed artwork/controller metadata.")

    ap.add_argument("--name", help="AppName shown in Steam (e.g. 'My Game')")
    ap.add_argument("--exe", help="Exe path (Steam usually stores this quoted on Windows)")
    ap.add_argument("--startdir", default="", help="Working directory (StartDir)")
    ap.add_argument("--icon", default="", help="Icon path")
    ap.add_argument("--launch", default="", help="LaunchOptions")
    ap.add_argument("--shortcutpath", default="", help="ShortcutPath")
    ap.add_argument("--tags", default="", help="Comma-separated tags (e.g. Installed,Ready TO Play)")
    ap.add_argument("--hidden", action="store_true", help="Set IsHidden=1")
    ap.add_argument("--no-overlay", action="store_true", help="Disable AllowOverlay")
    ap.add_argument("--no-desktop-config", action="store_true", help="Disable AllowDesktopConfig")
    ap.add_argument("--flatpak-app-id", dest="flatpak_app_id", default="", help="Optional FlatpakAppID to preserve/set (Steam Deck).")

    # Steam Input (localconfig.vdf): 0 disabled, 1 default (remove our entry), 2 enabled
    ap.add_argument(
        "--steam-input",
        dest="steam_input",
        default=None,
        choices=["0", "1", "2"],
        help="Write userdata/<steamid>/config/localconfig.vdf UseSteamControllerConfig: 0=disabled 1=default 2=enabled",
    )
    ap.add_argument(
        "--sspy-parser-id",
        dest="sspy_parser_id",
        default="0",
        help="Identifier stored alongside controller/localconfig entries (sspyParserId=p<id>). Default: 0",
    )

    ap.add_argument(
        "--template",
        nargs="?",
        const=DEFAULT_NEPTUNE_TEMPLATE,
        default=None,
        help=f"Set Steam Deck (neptune) controller template. If given without value, defaults to {DEFAULT_NEPTUNE_TEMPLATE}",
    )
    ap.add_argument(
        "--force-template",
        action="store_true",
        help="Overwrite existing controller_config entry (otherwise only set if missing).",
    )

    ap.add_argument(
        "--verify-only",
        action="store_true",
        help="Verify existing VDF formats are parseable and round-trippable; do not write anything.",
    )

    ap.add_argument(
        "--dump-json",
        action="store_true",
        help="Dump matching shortcut + controller + artwork info as JSON and exit (no writes).",
    )
    ap.add_argument("--dump-name", type=str, default=None, help="Shortcut AppName to dump (exact match, falls back to case-insensitive).")
    ap.add_argument("--dump-appid", type=str, default=None, help="Shortcut appid to dump (decimal or 0xhex).")
    ap.add_argument("--dump-list", action="store_true", help="Dump a list of shortcuts (parsed if possible, else raw scan) in JSON.")

    ap.add_argument("--grid", type=str, default=None, help="Grid PNG -> {appid}.png (writes both 32-bit and long-id)")
    ap.add_argument("--portrait", type=str, default=None, help="Portrait grid PNG -> {appid}p.png (writes both 32-bit and long-id)")
    ap.add_argument("--hero", type=str, default=None, help="Hero PNG -> {appid}_hero.png (writes both 32-bit and long-id)")
    ap.add_argument("--logo", type=str, default=None, help="Logo PNG -> {appid}_logo.png (writes both 32-bit and long-id)")
    ap.add_argument("--icon-file", type=str, default=None,
                    help="PNG icon to copy into userdata/<steamid>/config/grid/ and set as shortcut icon path")
    # Helpers for embedding in launchers (avoid JSON parsing in shell)
    ap.add_argument(
        "--print-detected-steamid",
        action="store_true",
        help="Print the most-recent SteamID folder and exit (auto-detect Steam root).",
    )
    ap.add_argument(
        "--is-installed-flatpak",
        type=str,
        default=None,
        help="Exit 0 if a shortcut with this FlatpakAppID exists (auto-detect Steam root + most recent user unless --steamid given). Exit 1 otherwise.",
    )

    ap.add_argument(
        "--is-installed",
        action="store_true",
        help="Exit 0 if a shortcut exists matching --name/--exe (and optionally --launch). Exit 1 otherwise.",
    )
    ap.add_argument(
        "--match-launch",
        action="store_true",
        help="With --is-installed: also require LaunchOptions to match --launch exactly.",
    )

    args = ap.parse_args()

    steam_root = Path(args.steam_root) if args.steam_root else detect_steam_root()
    if not steam_root:
        print("Could not auto-detect Steam root. Pass --steam-root.", file=sys.stderr)
        return 2

    # ---- helper modes for launchers / scripts ----
    if args.print_detected_steamid:
        try:
            _sr, sid = detect_recent_steamid()
            print(sid)
            return 0
        except Exception:
            return 1

    if args.is_installed_flatpak is not None:
        try:
            userdir, sid = choose_userdata_dir(steam_root, args.steamid)
            ok = is_flatpak_shortcut_installed(steam_root, sid, args.is_installed_flatpak)
            return 0 if ok else 1
        except Exception:
            return 1

    if args.remove_flatpak_app_id is not None:
        if args.all_steamids:
            ids = list_all_steamids(steam_root)
        else:
            try:
                _userdir, sid = choose_userdata_dir(steam_root, args.steamid)
                ids = [sid]
            except Exception as e:
                print(f"Could not select Steam user: {e}", file=sys.stderr)
                return 2

        rc_final = 0
        messages: List[str] = []
        for sid in ids:
            rc, msg = remove_flatpak_shortcut_for_steamid(steam_root, sid, args.remove_flatpak_app_id)
            if rc != 0:
                rc_final = rc
            messages.append(msg)
        if messages:
            print("\n".join(messages))
        if rc_final == 0:
            print("Restart Steam to see changes.")
        return rc_final

    if args.is_installed:
        try:
            userdir, sid = choose_userdata_dir(steam_root, args.steamid)
            vdf = steam_root / "userdata" / sid / "config" / "shortcuts.vdf"
            if not vdf.exists():
                return 1

            data = vdf.read_bytes()
            count, ok = _verify_shortcuts_vdf_roundtrip(data)
            if not ok:
                return 1

            shortcuts = parse_shortcuts_vdf(data)

            # Require --name and --exe (same inputs you use to create the shortcut)
            if not args.name or not args.exe:
                return 1

            want_name = str(args.name)
            want_exe = str(args.exe)
            want_launch = str(args.launch or "")

            def norm_exe(s: str) -> str:
                # Steam often stores exe quoted; normalize.
                s = (s or "").strip()
                if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
                    s = s[1:-1]
                return s

            for sc in shortcuts:
                if sc.app_name == want_name and norm_exe(sc.exe) == norm_exe(want_exe):
                    if args.match_launch and (sc.launch_options or "") != want_launch:
                        continue
                    return 0
            return 1
        except Exception:
            return 1

    if args.dump_json:
        if (args.dump_name is None) and (args.dump_appid is None) and (not args.dump_list):
            print("dump-json requires --dump-name or --dump-appid or --dump-list", file=sys.stderr)
            return 7

        dump_appid = None
        if args.dump_appid is not None:
            try:
                dump_appid = int(str(args.dump_appid), 0)
                # Normalize to signed int32 so 0x8xxxxxxx matches shortcuts.vdf appid
                dump_appid &= 0xFFFFFFFF
                if dump_appid >= 0x80000000:
                    dump_appid -= 0x100000000
            except Exception:
                dump_appid = None

        userdir, chosen_id = choose_userdata_dir(steam_root, args.steamid)
        cfg = userdir / "config"
        vdf_path = cfg / "shortcuts.vdf"
        grid_dir = cfg / "grid"

        return _dump_json(
            steam_root=steam_root,
            steamid=chosen_id,
            shortcuts_vdf=vdf_path,
            grid_dir=grid_dir,
            dump_name=args.dump_name,
            dump_appid=dump_appid,
            dump_list=bool(args.dump_list),
        )

    if not args.name or not args.exe:
        print("the following arguments are required: --name, --exe", file=sys.stderr)
        return 2

    if args.all_steamids:
        rc_final = 0
        msgs: List[str] = []
        for sid in list_all_steamids(steam_root):
            rc, msg = _apply_for_steamid(steam_root, sid, args)
            if rc != 0:
                rc_final = rc
            msgs.append(msg)
        print("\n\n".join(msgs))
        if not args.verify_only:
            print("Restart Steam to see changes.")
        return rc_final

    userdir, chosen_id = choose_userdata_dir(steam_root, args.steamid)
    rc, msg = _apply_for_steamid(steam_root, chosen_id, args)
    if msg:
        print(msg)
    if rc == 0 and (not args.verify_only):
        print("Restart Steam to see changes.")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())