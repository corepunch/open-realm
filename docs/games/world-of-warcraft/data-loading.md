# Data Loading

## Current Target Shape

The World of Warcraft target is built as `openwow`. It links WoW-specific game, renderer, and UI libraries through the same selected-game runtime boundary as the Warcraft III and StarCraft II targets.

Build:

```bash
make openwow
```

Run with the Makefile sample path:

```bash
make run-wow
```

Direct run:

```bash
build/bin/openwow -data data/world-of-warcraft +map World/Maps/Azeroth/Azeroth.wdt
```

The current data path expects a locally supplied WoW client install or extracted/installable MPQs. The repository does not contain retail client data.

## Installed Data Layout

The local workflow described by `tools/README.md` installs data under:

```text
data/world-of-warcraft/
```

Useful archive examples:

- `model.MPQ`
- `texture.MPQ`
- `dbc.MPQ`
- terrain/world archives containing `World/Maps/...`

The WoW target reads files through the engine filesystem/archive layer. Keep tests and fixtures independent from a developer's local data folder unless the test is explicitly a manual asset experiment.

## Map Entry

The map command accepts a WDT path:

```text
World/Maps/<MapName>/<MapName>.wdt
```

Examples:

```text
World/Maps/Azeroth/Azeroth.wdt
World/Maps/Kalimdor/Kalimdor.wdt
```

The common world path normalizes the map directory/name, then derives ADT paths:

```text
World/Maps/<MapName>/<MapName>_<tile_x>_<tile_y>.adt
```

The coordinate helper maps world coordinates to the 64x64 ADT grid with `32.0 - coord / 533.333313`.

## DBC Files

The current code reads classic-style `WDBC` files for targeted lookups rather than full DBC/DB2 coverage.

Known useful files:

- `DBFilesClient\Map.dbc`
- `DBFilesClient\WorldSafeLocs.dbc`
- `DBFilesClient\CharStartOutfit.dbc`
- `DBFilesClient\ItemDisplayInfo.dbc`
- `DBFilesClient\CharSections.dbc`
- `DBFilesClient\CharHairGeosets.dbc`
- `DBFilesClient\CharHairTextures.dbc`
- `DBFilesClient\HelmetGeosetVisData.dbc`

`Map.dbc` and `WorldSafeLocs.dbc` are used for map metadata and spawn/safe-location lookup. Character display work uses the character and item DBCs listed above.

Classic-era DBCs can report a logical field count larger than `record_size / 4`. Do not reject the whole file for that alone; validate the envelope and check each accessed field against `record_size`.

See [`docs/dbc-reference.md`](dbc-reference.md) for the WDBC container layout, the per-table field offsets, and the packed appearance/equipment bitfields.

## Tools

Inspect MPQs:

```bash
build/bin/mpqtool -mpq data/world-of-warcraft/model.MPQ ls
build/bin/mpqtool -mpq data/world-of-warcraft/dbc.MPQ cat DBFilesClient\\Map.dbc
```

Inspect or preview M2 models:

```bash
build/bin/m2tool \
  -mpq data/world-of-warcraft/model.MPQ \
  -model "Character\\Orc\\Male\\OrcMale.m2" \
  --info
```

DBC-backed player configuration preview:

```bash
build/bin/m2tool \
  -mpq data/world-of-warcraft/model.MPQ \
  -mpq data/world-of-warcraft/dbc.MPQ \
  -mpq data/world-of-warcraft/texture.MPQ \
  -model "Character\\Orc\\Male\\OrcMale.m2" \
  --wow-player-config-only
```

