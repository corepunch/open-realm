# Item Pickup Sandbox Map

`tools/wc3_make_item_pickup_sandbox.py` generates a local retail-compatible map for inspecting and picking up Warcraft III items. The generated map and its CSV roster stay under the ignored local `data/Warcraft III` tree; do not commit either generated file.

## Generate

The generator reads the installed TFT `War3x.mpq` item table and a retail Lost Temple melee map. Build the repository MPQ reader, then run:

```sh
make build/bin/mpqtool
python3 tools/wc3_make_item_pickup_sandbox.py
```

By default it writes:

- `data/Warcraft III/Maps/FrozenThrone/Scenario/Item Pickup Sandbox.w3x`
- `data/Warcraft III/Maps/FrozenThrone/Scenario/Item Pickup Sandbox roster.csv`

For another installation, pass `--data-dir '/path/to/Warcraft III'`. `--source-map` selects a different map relative to that data directory; the default is `Maps/FrozenThrone/(4)LostTemple.w3x`. `--output-dir` changes the output location. The `mpqtool` executable defaults to `build/bin/mpqtool`; `--mpqtool` and `--smpq` can override tool locations. The external `smpq` command must be installed and on `PATH` unless overridden.

## Items and placement

The generator includes every four-character item rawcode in retail `Units/ItemData.slk`, sorted by class and display name. That includes regular inventory items, campaign items, power-ups, and runes. The CSV records each item's rawcode, name, class, power-up flag, authored ability aliases, and map coordinates.

The source map supplies retail terrain, pathing, and the playable map shell. The generator finds dry, fully walkable positions and spreads the items across the map. Its item placements are recalculated from the source map data each time it runs. A controllable Paladin is added near Player 1's start location, and the camera starts there. Use the minimap to visit other item groups. Items with active effects can be picked up by the Paladin; power-ups follow their normal retail pickup behavior.

## Launch

Build and launch the generated map with the regular game client:

```sh
make build -j4 FFMPEG=1 && ./build/bin/openwarcraft3 \
  -data 'data/Warcraft III/' \
  +set vid_mode 9 +set vid_native 0 +set vid_fullscreen 0 \
  +set r_cursor 1 +set sv_cheats 1 +tft \
  +map 'Maps\\FrozenThrone\\Scenario\\Item Pickup Sandbox.w3x'
```

The `+tft` option is required for this map. The roster comes from the Frozen
Throne item data, and RoC mode hides `War3x.mpq`, including item icons and
descriptions for expansion items such as Moonstone. The item can still appear
and function when its gameplay data is available from another source, which
can make this look like an item-specific presentation defect.

The CSV roster is useful for locating a particular rawcode or confirming which retail items were included. The map exercises item placement and pickup interactively; it does not assert that every item's gameplay effect is implemented by OpenRealm.
