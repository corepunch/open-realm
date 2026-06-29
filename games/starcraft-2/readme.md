# StarCraft II

This is an alternate game target for StarCraft II data experiments. It exists mainly to keep the engine honest across more than one Blizzard RTS asset family and to exercise the M3 renderer path behind the same selected-game module boundary.

The code here owns a small game module and the StarCraft II M3 renderer hooks.

## Status

Prototype asset-rendering target.

`opensc2` builds, links, and provides a clean place for M3 work. It is not a playable StarCraft II implementation, and it does not yet have a StarCraft II UI or gameplay layer. The current value is technical: model format coverage, renderer integration, and a second RTS-shaped game module.

## Working

- Separate `opensc2` executable and game/renderer libraries.
- Selected-game module integration for StarCraft II via the shared engine build.
- M3 model loader entry point for StarCraft II model data.
- M3 material, reference table, sequence, keyframe interpolation, and shader scaffolding.
- Basic skeletal/skinned model render path through the compound renderer.
- Minimal game module that can initialize, load map collision through the shared map interface, and provide required game exports.
- Build integration through `make opensc2`.

## Partial

- M3 support is actively shaped around the renderer path and is not full StarCraft II asset parity.
- The game module is intentionally minimal and mostly acts as a host for map/model experiments.
- UI currently falls back to the default build shape; there is no StarCraft II-specific UI library.
- Map/world behavior is placeholder-level compared with the Warcraft III target.

## Not There Yet

- Playable StarCraft II gameplay.
- StarCraft II data table, trigger, ability, race, or campaign systems.
- Full SC2 map format support.
- Complete M3 material, animation, particle, attachment, and lighting fidelity.
- StarCraft II menus, HUD, editor-like behavior, or multiplayer flow.

## Build And Run

Build:

```bash
make opensc2
```

Run with the Makefile's sample StarCraft II data path and first Terran campaign map:

```bash
make run-sc2
```

Or run directly through map resolution:

```bash
build/bin/opensc2 -data data/StarCraft2 +map TRaynor01
```

## Notes

This target expects locally supplied StarCraft II data for real asset experiments. Original assets, names, and game data belong to Blizzard Entertainment. The directory is here so the engine can grow beyond one asset format without pretending the SC2 game is already built.

## Documentation

Public reverse-engineering and modding references for how StarCraft II maps are stored, opened, and rendered. Not Blizzard documentation and not a complete implementation spec — a map for loader and renderer work.

### Documents

- [Map Storage And Loading](docs/map-storage-and-loading.md) — container format, component folders, dependency/XML loading behavior, and cache/download context.
- [Embedded Map Files](docs/embedded-map-files.md) — full binary specs for all known files inside `.SC2Map` archives.
- [Map, Model, And Unit Data](docs/map-model-unit-data.md) — practical path from placed objects through catalog XML to M3 models.
- [Parser Notes](docs/parser-notes.md) — practical loading order and implementation guidance.
- [References](docs/references.md) — all public sources, tools, and GitHub repos used.
- [Sounds](docs/sounds.md)

### File Format Details

- [MapInfo](docs/file-formats/mapinfo.md) — complete `MapInfo` binary struct with all fields and player slot layout.
- [PlacedObjects](docs/file-formats/objects.md) — complete `<PlacedObjects>` XML schema.
- [Actors And Models](docs/file-formats/actors-and-models.md) — actor system, `CActorUnit` key fields, `CModel` catalog fields.
- [M3 Model Format](docs/file-formats/m3.md) — M3 binary model format: reference table, geometry, skeleton, animations, materials.

### Short Version

`.SC2Map`, `.SC2Mod`, `.SC2Archive`, and `.s2ma` files are MPQ archives. A map contains metadata, terrain/pathing binary layers, placed-object XML, minimap/loading assets, localized strings, trigger/Galaxy code, and map-local game-data XML.

### Implementation Status

| Area | Status |
| --- | --- |
| MPQ loading | done |
| `MapInfo` | done |
| `Objects` (units, doodads) | done |
| `t3Terrain.xml` (cliff sets, cells, textures) | done |
| `t3HeightMap` | done |
| `t3SyncHeightMap` (fine height detail) | done |
| `t3SyncCliffLevel` | done |
| `t3CellFlags` | done |
| `t3TextureMasks` | done |
| Terrain rendering (ground + cliff walls) | done |
| Cliff config canonicalization | done |
| `t3SyncPathingInfo` (pathing) | **not started** |
| `t3Water` | **not started** |
| `t3FluffDoodad` | **not started** |
| Catalog-driven unit → model resolution | **not started** (currently uses path guessing) |
| `.m3a` animation supplements | **not started** |
| Team-color texture swapping | **not started** |
