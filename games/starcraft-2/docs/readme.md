# StarCraft II Map Documentation

These notes collect public reverse-engineering and modding references for how StarCraft II maps are stored, opened, and rendered.

They are not Blizzard documentation and are not a complete implementation spec. Treat them as a map for loader and renderer work: enough to know which files to inspect, which community tools already parse useful pieces, and which areas remain uncertain.

## Documents

- [Map Storage And Loading](map-storage-and-loading.md) — container format, component folders, dependency/XML loading behavior, and cache/download context.
- [Embedded Map Files](embedded-map-files.md) — full binary specs for all known files inside `.SC2Map` archives (`t3HeightMap`, `t3SyncCliffLevel`, `t3CellFlags`, `t3TextureMasks`, `t3Water`, `t3Terrain.xml`, and more).
- [Map, Model, And Unit Data](map-model-unit-data.md) — practical path from placed objects through catalog XML to M3 models, plus staged terrain reconstruction notes.
- [Parser Notes](parser-notes.md) — practical loading order and implementation guidance for an OpenWarcraft3-style parser.
- [Catalog Layering](catalog-layering.md) — SC2 catalog dependency chain, load order, inheritance model, and map-local override semantics.
- [References](references.md) — all public sources, tools, and GitHub repos used to assemble these notes.

### File Format Details

- [file-formats/mapinfo.md](file-formats/mapinfo.md) — complete `MapInfo` binary struct with all fields and player slot layout.
- [file-formats/objects.md](file-formats/objects.md) — complete `<PlacedObjects>` XML schema: `Unit`, `Doodad`, `Point`, `Camera` with all attributes.
- [file-formats/actors-and-models.md](file-formats/actors-and-models.md) — actor system, `CActorUnit` key fields, `CModel` catalog fields, unit-to-model resolution pipeline.
- [file-formats/m3.md](file-formats/m3.md) — M3 binary model format: reference table, geometry, skeleton, animations, materials.

## Short Version

`.SC2Map`, `.SC2Mod`, `.SC2Archive`, and `.s2ma` files are MPQ archives. A map contains metadata, terrain/pathing binary layers, placed-object XML, minimap/loading assets, localized strings, trigger/Galaxy code, and map-local game-data XML. The Galaxy Editor can save a map as an `.SC2Components` folder, which is the same content unpacked into a directory.

**Loading order:**
1. Open archive (MPQ or `.SC2Components` directory)
2. Parse `MapInfo` for dimensions, bounds, tileset, player slots
3. Parse `GameStrings.txt` for localized name/description
4. Parse `Objects` for placed units, doodads, points, cameras
5. Parse `t3Terrain.xml` for height quantization params, cliff sets, texture layers, cliff cells
6. Parse binary terrain layers: `t3CellFlags` (authoritative map size + cliff holes), `t3HeightMap` (vertex heights), `t3SyncCliffLevel` (cliff tiers), `t3TextureMasks` (texture blend)
7. Parse catalog XML (`Base.SC2Data/GameData/*.xml`) with dependency layering (Core → Liberty → loose → map-local) to resolve unit types → actors → models
8. Load `.m3` models and optional `.m3a` animation supplements

**Unit rendering resolution chain:**
```
Objects <Unit unitType="X"> → CUnit → CActorUnit → CModel → .m3 path + .m3a files
```

## Implementation Status

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
| Catalog-driven unit → model resolution | done (with dependency layering) |
| `.m3a` animation supplements | **not started** |
| Team-color texture swapping | **not started** |
