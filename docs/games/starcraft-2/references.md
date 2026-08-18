# References

Collected through 2026-06-06.

## Binary Format Specifications

- SC2Mapster Wiki, `File Formats/Maps`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps
  - Index of all embedded map file formats.
- SC2Mapster Wiki, `File Formats/Maps/MapInfo`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/MapInfo
  - Full `MapInfo` C struct, player slot layout, loader image fields.
- SC2Mapster Wiki, `File Formats/Maps/Objects`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/Objects
  - `<PlacedObjects>` XML schema, all attributes for `Unit`, `Doodad`, `Point`, `Camera`.
- SC2Mapster Wiki, `File Formats/Maps/t3HeightMap`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3HeightMap
  - `HMAP` header, 6-byte CHUNK layout, height decode formula, standard cliff elevations.
- SC2Mapster Wiki, `File Formats/Maps/t3SyncHeightMap`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncHeightMap
  - `SMAP` header, signed `int16` height offsets relative to main height level, /256 formula.
- SC2Mapster Wiki, `File Formats/Maps/t3SyncCliffLevel`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncCliffLevel
  - `CLIF` header, `USHORT height[sizeX*sizeY]` cliff level array.
- SC2Mapster Wiki, `File Formats/Maps/t3CellFlags`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3CellFlags
  - `LFCT` header, per-cell byte flags, `0x03` cliff-hole behavior.
- SC2Mapster Wiki, `File Formats/Maps/t3TextureMasks`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3TextureMasks
  - `MASK` header, nibble-packed 64×64-pixel blocks, layer count formula.
- SC2Mapster Wiki, `File Formats/Maps/t3Water`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3Water
  - `WATR` header, water rectangle CHUNKs, template reference to `WaterData.xml`.
- SC2Mapster Wiki, `File Formats/Maps/t3Terrain`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3Terrain
  - `t3Terrain.xml` schema: `<textureList>`, `<masks>`, texture name list.
- SC2Mapster Wiki, `File Formats/Maps/t3FluffDoodad`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3FluffDoodad
  - `DLFT` header, per-doodad `(x, y, type)` float+byte records.
- SC2Mapster Wiki, `File Formats/Maps/t3SyncPathingInfo`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3SyncPathingInfo
  - `PATH` header, `USHORT` per-cell pathing values (0x00=passable, 0x02=ramp, 0x23=blocked).
- SC2Mapster Wiki, `File Formats/Maps/t3HardTile`: https://sc2mapster.wiki.gg/wiki/File_Formats/Maps/t3HardTile
  - `HRDT` header, BLOCK/SUBBLOCK structs with position, 3×3 rotation matrix, scale.
- SC2Mapster Wiki, `File Formats` (top-level): https://sc2mapster.wiki.gg/wiki/File_Formats
  - Container formats (MPQ, SC2Map, SC2Mod, SC2Replay), asset formats (DDS, M3, M3A, OGV, TGA).
- SC2Mapster Wiki, `Model Files`: https://sc2mapster.wiki.gg/wiki/Model_Files
  - `.m3` (model + animations), `.m3a` (supplemental bone animations), model component list.
- StarCraft II Art Tools, `Appendix: Cliff Models`: https://mapster.talv.space/star-tools/Appendix_Cliffs.html
  - Cliff object naming, 200×200 tiling footprint, bottom-left/bottom-right/top-right/top-left corner order, variation suffix semantics, and vertex-alpha/alpha-mask requirements for cliff tops and bottoms.
- StarCraft II Art Tools, `Materials: Material Types: SC2 Terrain`: https://mapster.talv.space/star-tools/Material_SC2Terrain.html
  - Terrain material behavior for models that show terrain colors through a material or mask.

## Catalog / Data System

- SC2Mapster Wiki, `Data/Actors`: https://sc2mapster.wiki.gg/wiki/Data/Actors
  - Actor system overview, 70+ actor subtypes, Site Operations, Tokens, Actor Events.
- SC2Mapster Wiki, `Data/Actors/Unit`: https://sc2mapster.wiki.gg/wiki/Data/Actors/Unit
  - Full 145-field unit actor reference: Model, sounds, portrait, selection, minimap icon, animations, wireframe, status bars.
- SC2Mapster Wiki, `Data/Models/Generic`: https://sc2mapster.wiki.gg/wiki/Data/Models/Generic
  - Full 127-field model catalog: Art/Model path, animations, .m3a, flags, tipability, variations, texture declarations, physics.
- StarCraft II Editor Guides, `Data Spaces`: https://s2editor-guides.readthedocs.io/New_Tutorials/04_Data_Editor/data-spaces/
  - How to create data space XML files, `GameData.xml` includes, `.SC2Components` workflow, localization separation.

## Archive / Container Format

- Zezula, `MPQ Archives — MPQ file format`: https://www.zezula.net/en/mpq/mpqformat.html
  - Canonical public MPQ format reference. Notes MPQ user data is common in StarCraft II custom maps.
- Zezula, `Storm.dll`: https://www.zezula.net/en/mpq/stormdll.html
  - Blizzard maps, including StarCraft II maps, are MPQ archives.
- StormLib: https://www.zezula.net/en/mpq/stormlib.html
  - Open-source MPQ library for opening, listing, and extracting archives.
- SC2Mapster Community, `Getting Started — Working with SC2`: https://sc2mapster.github.io/mkdocs/setup/
  - `.SC2Map` vs `.SC2Components` archived vs unarchived project forms.

## Existing Parsers And Analysis Tools

- `sc2reader` GitHub: https://github.com/ggtracker/sc2reader
  - Python library for SC2 replays and maps. Parses `MapInfo`, `GameStrings.txt`, minimap, player slots, tileset. Does **not** parse terrain geometry.
- `sc2reader` docs: https://sc2reader.readthedocs.io/
  - API docs, `load_map`, `Map` resource, `MapInfo`/`MapInfoPlayer` support objects.
- RFEphemeration/sc2-map-analyzer: https://github.com/RFEphemeration/sc2-map-analyzer
  - C++ tool (SC2 v1.5.2 era). Parses `MapInfo`, `t3HeightMap`, `t3CellFlags`, `PaintedPathingLayer`, `t3Terrain.xml` (ramp list), `Objects`. Source files `read.cpp` and `SC2Map.hpp` are the best available open reference for binary parsing.
- CascLib (CASC archive reader): https://github.com/ladislav-zezula/CascLib
  - Open-source C library for reading CASC storage from Blizzard games (WoW, SC2, Diablo III, etc.). Use for extracting SC2 assets from the installed game.

## Blizzard Official

- Blizzard/s2protocol: https://github.com/Blizzard/s2protocol
  - Official Python library decoding SC2 replay protocol (`.SC2Replay`) into data structures. Covers replay events only — does not cover map terrain or binary terrain formats.

## Related SC2 Tooling

- Talv/sc2-dood: https://github.com/Talv/sc2-dood
  - TypeScript parser and exporter for the `Objects` file from `.SC2Map` archives.
- Talv/plaxtony: https://github.com/Talv/plaxtony
  - TypeScript libraries for SC2 map files: Galaxy Script, Triggers scheme, Game Data XML.
- Talv/stormex: https://github.com/Talv/stormex
  - CLI for listing and extracting files from Blizzard CASC storage.
- SC2Mapster/m3addon: https://github.com/SC2Mapster/m3addon
  - Blender M3 importer/exporter. `structures.xml` documents variable M3 vertex formats, vertex color fields, material vertex-color/vertex-alpha flags, standard materials, composite materials, and terrain material records.
- flowtsohg/mdx-m3-viewer: https://github.com/flowtsohg/mdx-m3-viewer
  - Web M3 renderer/parser. Useful reference for deriving M3 vertex size from `vertexFlags`, preserving material references per batch, and applying vertex color in shaders.

## Caution

Many sources are old, community-maintained, or beta-era. Validate every binary layout against current maps before depending on it in code. The sc2-map-analyzer source targets SC2 v1.5.2 (~2012); later patches may have changed field layouts.
