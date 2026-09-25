# Parity harness

Tools for keeping the reimplementation visually faithful to the original
Warcraft III, and for catching rendering regressions.

## Linux retail comparison with Wine

`wc3.sh` launches retail or OpenRealm against the same installation and optional map, in a window. It records
the command, edition, map, source revision/dirty files, Wine version, and process output under `build/parity/logs/`.
Retail requires your own installed executable and archives. No disassembler or debugger is required.

The edition defaults to TFT and the destination defaults to the menu. The local `build/parity/wc3`
wrapper supplies `WC3DATA` and forwards all arguments to this script:

```bash
./build/parity/wc3 openrealm                    # TFT menu
./build/parity/wc3 openrealm --map=menu         # same destination
./build/parity/wc3 openrealm --map=elf1         # Maiev, Rise of the Naga
./build/parity/wc3 openrealm roc --map=elf1     # Tyrande, Enemies at the Gate
./build/parity/wc3 openrealm --map=elf1 --intro # normal cinematic timing
./build/parity/wc3 openrealm --list           # all maps with in-game names
./build/parity/wc3 openrealm --select         # searchable terminal picker
./build/parity/wc3 openrealm --map=roc-human2-interlude
./build/parity/wc3 openrealm --map=tft-undead7b
./build/parity/wc3 openrealm --map=tft-orc2-10  # Outland Arena
./build/parity/wc3 openrealm --select --maps-dir='/path/to/optional/maps'
```

All slugs are generated from installed map identifiers, not a compiled campaign list. Unqualified
slugs use the selected edition; `roc-` and `tft-` prefixes override it. `--list` and `--select`
show both editions. Type any words in the picker to search names, campaign, path, or slug;
use arrows/Page Up/Page Down to navigate, Enter to launch, Escape to cancel, and Ctrl-U to clear.
The picker uses a gold selection bar, cyan ROC labels, violet TFT labels, and a selected-map
detail area. It uses the terminal background, adapts its palette to 256/8-color terminals,
falls back to reverse-video selection without color, and switches to a compact layout in small windows.
Optional `--maps-dir=PATH` folders are scanned recursively for `.w3m`/`.w3x` files and may be repeated.
Their `custom-<folder>-<map>` aliases appear alongside campaigns, using W3I/WTS names. Native loose
map paths are relative to the data directory (including `../` for external folders); retail receives
the same path relative to its installation. No map files are copied or modified.

Aliases enable native `skip_cutscene=1` to fast-forward into gameplay through the map's authored
script cleanup. This cvar also shortens later cinematic timing; use `--intro` to retain normal
timing, or set `skip_cutscene 0` in the console after the opening. Retail still requires manual Escape.
Custom `--map='Maps/My Map.w3x'` paths and legacy positional paths retain normal timing.
Unknown aliases and duplicate map/edition arguments fail explicitly. The wrapper is local and ignored
by Git; keep its installation path there, not in the shared script.

Run command-contract tests without opening a game: `make test-render-harness`.

### Catalog generation

`wc3_maps.py` reads `UI/CampaignStrings.txt` and `UI/CampaignStrings_exp.txt` with INI/CSV parsers.
Older tables use `TitleN`/`MissionN`/`FileN`; newer tables (including newer ROC tables) use CSV
`MissionN` rows containing title, name, path, and optional extra fields. Schema selection follows
the fields present, not the edition. Archive listfiles contribute unlisted campaign maps, and
`war3map.w3i`/`war3map.wts` supply their loading titles via the existing map-audit parser.
TFT's model-only finale and prerendered movies are not map destinations.

Archives are discovered case-insensitively in the installation root and `Frozen Throne/`, in
base/local/expansion/expansion-local/patch precedence. Some retail patch archives have no listfile;
the generator logs that limitation and follows literal next-map references plus the bonus campaign's
authored `udg_ZoneMapPath`, `udg_ZoneMapExt`, and `udg_ZoneMaps[]` table. This discovers the Orc bonus
submaps without guessing filenames. Windows-1252 legacy script/WTS decoding is reported explicitly.
The observed 1.27b installation yields 44 ROC + 53 TFT maps; other editions follow their own data.

The generated catalog is `build/parity/campaign-maps.json`. Archive, optional-map, tool, and generator
file timestamps/sizes invalidate the cache; `--refresh` forces regeneration. The first generation
extracts nested metadata and can take a minute. Later listing/selection uses the cache. Python 3 with
`curses` and a built `mpqtool` are required; no extra Python packages are needed. `WC3_MAP_CATALOG`
selects an explicit catalog for fixtures or a separately generated catalog (its freshness is then
the caller's responsibility). Missing maps, unreadable metadata, and alias collisions fail visibly.
See [MPQ decoding](../../docs/fs-loading-architecture.md) and
[vendored dependencies](../../docs/vendored-dependencies.md) for PKWARE support.

```bash
export WC3DATA='/path/to/Warcraft III'
# TFT Night Elf 1 (Maiev), as distinct from ROC Night Elf 1 (Tyrande):
tools/parity/wc3.sh retail tft 'Maps/FrozenThrone/Campaign/NightElfX01.w3x'
tools/parity/wc3.sh openrealm tft 'Maps/FrozenThrone/Campaign/NightElfX01.w3x'
# Reign of Chaos:
tools/parity/wc3.sh retail roc 'Maps/Campaign/NightElf01.w3m'
tools/parity/wc3.sh openrealm roc 'Maps/Campaign/NightElf01.w3m'
# Omit the map to use the menus; print commands without launching:
WC3_DRY_RUN=1 tools/parity/wc3.sh retail tft
```

Build the native executable first with `make BUILD=release FFMPEG=1 openwarcraft3`. `WC3_BINARY` can select a
different build. Install Wine with your distribution's package manager (Arch/CachyOS: `sudo pacman -S wine gst-plugins-good`).
Wine's AVI demuxer may also need `gst-plugins-good`; a `Missing decoder: Audio Video Interleave` log means movie
playback cannot be used as evidence yet. Use a consistent retail patch and locale for each comparison.

The default prefix is `${XDG_DATA_HOME:-$HOME/.local/share}/open-realm/wine-wc3`, separate from `~/.wine`.
`WINEPREFIX` and `WINE` override the prefix and runner. Modern Wine can run the 32-bit game in its default
64-bit/WoW64 prefix; do not force `WINEARCH=win32` on a WoW64-only distribution build.
See the upstream [Wine manual](https://man.archlinux.org/man/wine.1.en) for prefix and argument semantics.
The launcher targets the classic `war3.exe` installation layout (locally exercised with 1.27b), uses `-classic`
for ROC, and passes archive-relative map paths to `-loadfile`. It does not install games, alter archives, or
unlock campaign progress. Retail can write its normal settings, saves, and replays in the installation/prefix.

On CachyOS with Wine 11.17, Warcraft III 1.27b build 7085 reached NightElfX01's loading screen and
`PRESS ANY KEY TO CONTINUE` using the default renderer after installing the AVI plugin. The initial run with
forced OpenGL and missing AVI support crashed in `Game.dll`; that run is not a valid reference result. The
launcher therefore leaves the renderer at its default. `WC3_RENDERER=opengl` explicitly opts into the alternate
path for separate compatibility testing. A successful loading screen proves startup/map loading, not audio or
gameplay fidelity. Warcraft's own crash reports are under
`$WINEPREFIX/drive_c/users/<user>/AppData/Local/Temp/BlizzardError/<timestamp>/Crash.txt`.

For bark comparisons, record both runs with desktop audio and perform the same short sequence after skipping
the intro: select one unit, wait for speech to finish, issue one ground move, then repeat moves while it speaks.
Repeat with a mixed selection, changing the focused subgroup, and with an attack order. Record speaker, response
category, overlap/interruption, and order acceptance separately. Keep sound/music volume, camera position,
selection, and elapsed time comparable; random line identity need not match. Save a retail game just after the
intro for fast manual repetition, and keep native/retail saves separate.

The logs capture diagnostics, not audio. Use OBS or your desktop recorder for audible evidence. When an observed
difference becomes understood, encode it in a fixture-backed regression; do not replace the automated tests with
repeated manual launches. Existing sound-path regressions and known limits are documented in
[WC3 sounds](../../games/warcraft-3/sounds.md).

## 1. `shot.sh` — live window capture (macOS)

Launches a client, captures its GL window to a PNG by window-id (reliable even
when the window is occluded), then kills it.

```sh
tools/parity/shot.sh --app ours   --screen menu_main -o /tmp/ours.png
tools/parity/shot.sh --app legacy                    -o /tmp/legacy.png
tools/parity/shot.sh --app both   -o /tmp/parity.png   # ours + Legacy, side by side*
```

`--app both` writes `*-ours.png` and `*-legacy.png`; if ImageMagick's `montage`
is installed it also writes `*-sidebyside.png`. Options: `--data <dir>`,
`--screen <menu_cmd>`, `--delay <sec>`, `--keep`.

The Legacy client is the original at
`/Applications/Warcraft III (Legacy)/Warcraft III.app` (v1.29.2) — the parity
reference.

## 2. `render_golden.sh` — golden-image regression test

Renders each model in `golden_manifest.txt` to a **deterministic** PNG via
`mdxtool -o` (fixed frame + seeded particle RNG → byte-stable output) and
compares it to a committed reference in `golden/` with `imgdiff`. Fails if any
render drifts beyond the mean-pixel-difference threshold.

```sh
make test-render-golden        # compare against golden/ (exit non-zero on drift)
make update-render-golden      # regenerate golden/ after an intentional change
# or directly:
tools/parity/render_golden.sh [--update] [--threshold 2.0] [--data <dir>]
```

This requires a display/GL (mdxtool opens a window), so it is **opt-in** and not
part of `make test` (CI is headless).

### Manifest format

`name | mpq | model | extra mdxtool args` — one render per line. Pick models
self-contained in one archive that exercise distinct renderer paths (flipbook
water, team color, particles, geometry). Glue scenes need `--use-model-camera`;
units use the default fitted orbit camera.

When a comparison fails, the offending render and an amplified diff are written
to `tools/parity/_last_fail_<name>.png` / `_last_fail_<name>.diff.png` for
inspection.

## Pieces

- `mdxtool -o <png> [--frame <ms>] [--seed <n>]` — deterministic clean render to
  PNG (in `tools/mdxtool.c`).
- `imgdiff a b [--threshold m] [--pixel-tol t] [--diff out.png]` — image compare
  (in `tools/imgdiff.c`; standalone, stb-only).
- `golden/` — committed reference PNGs (regenerate with `--update`).
