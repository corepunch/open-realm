# Creep Ability Sandbox Map

`tools/wc3_make_creep_ability_sandbox.py` generates a local custom map for inspecting the retail neutral-hostile units that have abilities. The generated map and roster are kept under the ignored local `data/Warcraft III` tree; do not add the `.w3x` or generated CSV to a source commit.

## Generate

The generator reads the installed TFT `War3x.mpq` tables and a retail Lost Temple melee map. Build the repository MPQ reader, then run:

```sh
make build/bin/mpqtool
python3 tools/wc3_make_creep_ability_sandbox.py
```

By default it writes:

- `data/Warcraft III/Maps/FrozenThrone/Scenario/Creep Ability Sandbox.w3x`
- `data/Warcraft III/Maps/FrozenThrone/Scenario/Creep Ability Sandbox roster.csv`

For a non-default installation, pass `--data-dir '/path/to/Warcraft III'`. `--source-map` selects a different map relative to that data directory; the default is `Maps/FrozenThrone/(4)LostTemple.w3x`. `--output-dir` changes the output location. The `mpqtool` executable defaults to `build/bin/mpqtool`; `--mpqtool` and `--smpq` can override tool locations. The external `smpq` command must be installed and on `PATH` unless overridden.

## Roster and placement

The generator includes a rawcode only when all of these retail table conditions hold:

- its rawcode begins with `n` and has four characters;
- `UnitData.slk` marks it valid;
- `UnitUI.slk` has `hostilePal=1`;
- `UnitAbilities.slk` has a non-empty `abilList`.

The hostile-player UI flag avoids pulling in neutral heroes, campaign-only units, and summons just because their balance row uses the creep race. The CSV records each included unit's rawcode, retail name, unit class, and authored ability aliases for lookup.

The source map supplies terrain, pathing, and the retail map shell. The script reads `war3map.wpm` and `war3map.w3e`, then selects low-blocking, dry regions distributed across the map. Each region contains up to 12 units in a 3-by-4 layout with 320 world units between neighboring positions. Before spawning, the injected JASS removes preplaced units and destructables only from those spawn regions. It creates the creeps for Player 1 and moves the camera to the map center. The layout is recalculated from the source map data each time the script runs.

## Launch and inspect

Build the game, then launch the generated map with the regular game client:

```sh
make build -j4 FFMPEG=1 && ./build/bin/openwarcraft3 \
  -data 'data/Warcraft III/' \
  +set vid_mode 9 +set vid_native 0 +set vid_fullscreen 0 \
  +set r_cursor 1 +set sv_cheats 1 \
  +map 'Maps\\FrozenThrone\\Scenario\\Creep Ability Sandbox.w3x'
```

The units are distributed in separate groups across the map. Use the minimap to move between groups; select a unit to inspect its command card and abilities. The `unit enemy`, `unit own`, and `unit sleep` developer cheats are documented in [Cheat Commands](../../cheat-commands.md) and can help exercise enemy-targeted effects.

The bounded OpenRealm startup smoke audit can be run with:

```sh
python3 tools/wc3_map_audit.py \
  --data 'data/Warcraft III' \
  --binary build/bin/openwarcraft3 \
  --mpqtool build/bin/mpqtool \
  --jobs 1 --frames 20 --timeout 60 \
  --loose-map 'data/Warcraft III/Maps/FrozenThrone/Scenario/Creep Ability Sandbox.w3x' \
  --output-dir build/wc3-creep-sandbox-audit
```

Reaching the frame limit checks startup/runtime health only. It does not verify every unit's visual placement or ability behavior; use a retail game launch for those interactive checks.
