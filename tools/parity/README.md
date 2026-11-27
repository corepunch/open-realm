# Parity harness

Tools for launching maps and keeping the reimplementation visually faithful to
the original games, including rendering regression checks.

## StarCraft II map picker

`sc2.sh` uses the same terminal picker as Warcraft III. Its catalog is built from
installed `.SC2Maps` archive listfiles and loose `.SC2Map` directories. Display
names come from the map's `DocInfo/Name` game string.

```bash
make mpqtool opensc2
SC2DATA=data/StarCraft2 tools/parity/sc2.sh --list
SC2DATA=data/StarCraft2 tools/parity/sc2.sh --select
SC2DATA=data/StarCraft2 tools/parity/sc2.sh --map=traynor01
SC2DATA=data/StarCraft2 SC2_DRY_RUN=1 tools/parity/sc2.sh --map=traynor01
make test-sc2-parity
```

`--refresh` rebuilds `build/parity/sc2-maps.json`. `SC2_BINARY` selects another
OpenRealm binary and `SC2_PARITY_LOGS` selects the log directory. Loose
`.SC2Map` and `.SC2Components` maps below `SC2DATA` are included. See the
[SC2 map picker contract](../../docs/games/starcraft-2/map-picker.md).

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
The shared picker implementation is `map_picker.py`; game-specific catalogs own map discovery and aliases.
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

## Headless UI driver (Linux): `wc3_drive.sh`

`wc3_drive.sh` runs OpenRealm on its own headless [gamescope](https://github.com/ValveSoftware/gamescope) display and
sends real X11 pointer and keyboard input with `xdotool`. Nothing appears on, or reads from, your desktop session, so it
also works over SSH. Use it for what the headless test suite cannot see: the full process with real data archives, real
SDL input, screen transitions over many frames and a chosen resolution. Typical uses are reproducing a menu bug report as
a replayable file, checking a UI change at several resolutions, and attaching identical screenshots to a PR. When a
difference is understood, still encode it in a fixture-backed test.

Menu steps address frames by name through the `uidump` and `click` console commands
([UI authoring](../../docs/ui-authoring.md#console-diagnostics-uidump-and-click)). `start` exports
`OPENREALM_UI_TEST=1`, which the game requires before it registers `click`. A name-based step does not depend on
layout, resolution or aspect, and `uiwait` polls the frame table instead of sleeping for a guessed time.

```bash
make BUILD=release openwarcraft3                 # needs gamescope, gamescopectl and xdotool installed
export WC3DATA='/path/to/Warcraft III'
tools/parity/wc3_drive.sh start -roc             # returns once the engine has initialized
tools/parity/wc3_drive.sh uiwait SinglePlayerButton 40
tools/parity/wc3_drive.sh ui Button              # UIFRAME lines: name, visibility, hit test, rects
tools/parity/wc3_drive.sh uiclick SinglePlayerButton
tools/parity/wc3_drive.sh shot single-player     # prints build/parity/drive/shots/single-player.png
tools/parity/wc3_drive.sh run tools/parity/scenarios/menu-custom-game.txt
tools/parity/wc3_drive.sh stop
```

A scenario is a text file with one driver verb per line (`uiwait`, `uiclick`, `ui`, `click X Y`, `key`, `type`,
`chat`, `cmd`, `shot`, ...), `wait SECONDS` and `#` comments. Lines are split on whitespace without shell quoting;
verbs that take text (`type`, `chat`, `cmd`, `waitlog`) use the rest of the line. `run` stops at the first failing
line and names it: `uiclick` fails unless the game answers `UICLICK ok`, `uiwait` fails when no frame with exactly
that name reports `visible=1 enabled=1 interactive=1 hit=1` in time (the same checks `click` makes), and
`waitlog SECONDS REGEX` fails when no `game.log` line after the last `mark` matches the extended regex. Any failed
input action fails its step, and so does a malformed `wait`. `as NAME VERB ...` runs one verb on a second instance
whose state is `$WC3_DRIVE_STATE-NAME`, so one file can drive a LAN host and guest. When a step fails or the run is
interrupted, `run` stops every instance the scenario itself started (`start`, `as NAME start`) and keeps their logs
and screenshots. `wc3_drive.sh --help` lists every verb.

| Scenario | Start with | Checks |
|----------|------------|--------|
| `menu-custom-game.txt` | `start -roc` or `-tft` | main menu, Single Player, custom game, map list, back |
| `options-round-trip.txt` | `start -roc` or `-tft` | Video, Sound and Gameplay panels lay out their controls; Cancel returns |
| `lan-join.txt` | nothing (it starts host and guest) | host creates a LAN game on a private `game_port`, a second instance joins, the host lobby reports that guest by name; the guest cancels, the slot opens, and it joins again |
| `map-start.txt` | nothing (it starts the game) | `+map` start reaches `G_ClientBegin`, screenshot of the opening view |

All scenarios use frame names and English engine log lines only, so they do not depend on the data's language or
on which maps are installed.

| Variable | Default | Meaning |
|----------|---------|---------|
| `WC3DATA` | required for `start` | Warcraft III installation passed as `-data` |
| `WC3_BINARY` | `build/bin/openwarcraft3` | engine binary |
| `WC3_DRIVE_STATE` | `build/parity/drive` | game log, screenshots and an isolated `XDG_DATA_HOME`; use one per instance |
| `WC3_DRIVE_WIDTH`, `WC3_DRIVE_HEIGHT` | `1280`, `800` | virtual screen size; pixel coordinates refer to it |
| `WC3_DRIVE_TIMEOUT` | `60` | seconds `start` waits for `OpenWarcraft3 initialized` |

Engine arguments after `start` are passed through (`-roc`, `-tft`, `+map ...`). Two instances, e.g. a LAN host and
guest, need two `WC3_DRIVE_STATE` directories. Screenshots come from `gamescopectl screenshot`, i.e. the composited
output. Console output reaches the driver through the game's stderr, which `start` writes to `game.log`.
The console opens with the key that types `` ` `` on a US keyboard, so `start` pins the private display to the US
layout (`XKB_DEFAULT_LAYOUT=us`); a German or French desktop layout would otherwise remap that key and every console
verb would time out.

Limits: Linux only. The in-game HUD has no frame names yet, so in-game steps still need `click X Y` in virtual-screen
pixels. The retail client is not driven by this script. The first click right after a screen change can be lost;
`uiwait` before `uiclick` avoids that. `make test-render-harness` runs `tests/test_parity_drive.py`, which checks the
command contract with fake `gamescope`/`xdotool` executables and needs neither a display nor game data.

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
