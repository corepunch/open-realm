# Map, Model, And Unit Data

This document turns the public references into a practical data-resolution plan for OpenWarcraft3. The important lesson is that a placed SC2 unit usually is not enough by itself to find an `.m3` file. The map places a unit ID; catalog XML resolves that unit through actors and models; model data then resolves art paths, animation add-ons, scale, selection, shadows, texture swap declarations, and attachment points.

## Data Layers

| Layer | Typical Files | What To Extract First |
| --- | --- | --- |
| Container | `.SC2Map`, `.SC2Mod`, `.s2ma`, `.SC2Components` | Archive listing, dependency/content roots, normalized paths. |
| Map metadata | `MapInfo`, localized `GameStrings.txt` | Map dimensions, bounds, tileset/theme, player slots, title/author/description. |
| Placement | `Objects` | Placed `Unit`, `Doodad`, `Point`, `Camera`, position, rotation, scale, player, resources. |
| Terrain | `t3HeightMap`, `t3SyncHeightMap`, `t3SyncCliffLevel`, `t3CellFlags`, `t3TextureMasks`, `t3Water`, `t3Terrain.xml` | Height/cliff grids, holes/cell flags, texture layer masks, water rectangles/templates. |
| Catalog XML | `Base.SC2Data/GameData/*.xml`, dependency mods | `UnitData`, `ActorData`, `ModelData`, terrain/water/texture data, buttons/requirements later. |
| Assets | `.m3`, `.m3a`, `.dds`, `.tga`, sounds, layouts | Model geometry/animations, supplementary animation sets, material textures, portraits/icons. |

## Map Placement: `Objects`

SC2Mapster documents `Objects` as a `<PlacedObjects>` XML file. The documented children include:

- `Unit`: `id`, `position`, `rotation`, `scale`, `unitType`, `player`, `resources`, plus AI/flag children.
- `Doodad`: `id`, `position`, `rotation`, `scale`, `type`, `tintColor`, plus flags.
- `Point`: `id`, `position`, `rotation`, `scale`, `type`, `name`, `model`, `animProps`, `sound`, `objectID`, pathing radii.
- `Camera`: `id`, `position`, `rotation`, `scale`, `name`, `target`, `CameraValue`, flags.

Implementation guidance:

- Parse `position` as a three-component value and preserve the raw text for diagnostics.
- Accept both the documented attribute style and simple fixture-friendly `x/y/z` attributes.
- Treat `id`/`unitType` as catalog keys, not model paths.
- A `Point` can carry a direct model field; a `Unit` normally should resolve through catalogs.
- Placed resources are units too in many maps, so resource/base detection should happen after unit catalog resolution, not as a separate hardcoded object kind.

## Unit To Model Resolution

The useful catalog path is:

```text
Objects Unit id/unitType
  -> Unit catalog entry, usually CUnit
  -> unit actor, usually CActorUnit
  -> model token / hosted model / build/death/portrait models
  -> Model catalog entry, usually CModel
  -> Art: Model path (.m3), optional/required .m3a animation files, scale/radius/shadow/texture metadata
```

SC2Mapster's actor docs describe unit actors as the main visual form for units: they drive model display, click/order sounds, armor textures, selection picture, portrait, and minimap icon. That means a renderer should avoid assuming `Assets\Units\Terran\<Unit>\<Unit>.m3` except as a fallback for tiny fixtures.

Catalog extraction order:

1. Read `GameData.xml` includes under `Base.SC2Data`.
2. Load catalog XML in include order.
3. Layer dependencies before map-local data.
4. Apply map-local modifications after dependency/base data.
5. Resolve localized display text separately from catalog IDs.

For a first resolver, collect only the minimum fields:

- Unit: ID, life/radius/mover/footprint, actor reference if explicit, resource flags/amounts if available.
- Actor: unit name/token, hosted model/model ID, portrait/icon/minimap fields where easy.
- Model: model file path, low-quality path, variation count, selection radius, shadow radius, scale min/max, `.m3a` animation files, texture declaration slots.

## Model Data And M3

SC2Mapster's file-format notes identify `.m3` as the model-plus-animation format and `.m3a` as supplemental bone animation data. ModelData fields add important runtime policy around that raw asset:

- `Art: Model` points to the model `.m3`.
- Optional and required animation fields can add `.m3a` files.
- `Walk Animation Move Speed` calibrates walk animation against terrain tiles per second.
- `Selection Radius`, `Shadow Radius`, `Visual Radius`, and model scale fields affect selection, shadows, culling, and footprint-like UI.
- `Tipability` fields describe how far a model may tilt over uneven terrain.
- Texture declaration fields define texture swap slots and substrings used by actor texture-select events.
- Variation fields define model variants and probabilities; do not collapse variants at load time.

OpenWarcraft3 already has M3 geometry/animation loading. The next useful step is not more filename guessing; it is catalog-driven model lookup plus optional `.m3a` support.

## Terrain Reconstruction

The public terrain notes are strong enough for staged parser work:

| File | Magic | Version Seen | Payload |
| --- | --- | --- | --- |
| `t3HeightMap` | `HMAP` | `101` | `sizeX`, `sizeY`, then chunks with `heightAdjustments`, `heightBase`, `mask`. |
| `t3SyncHeightMap` | `SMAP` | `102` | smaller signed height offsets; height value divided by 256 gives level-like offset. |
| `t3SyncCliffLevel` | `CLIF` | `100` | `sizeX`, `sizeY`, then `USHORT height[sizeX * sizeY]`. |
| `t3CellFlags` | `LFCT` | `101` | cell type bytes; useful for holes/visualization, not authoritative pathing. |
| `t3TextureMasks` | `MASK` | `102` | per-texture layer masks; texture definitions come from `t3Terrain.xml`. |
| `t3Water` | `WATR` | `110` | water/lava rectangles with template names resolved through `WaterData.xml`. |

SC2Mapster gives the `t3HeightMap` level formula:

```text
level = (heightAdjustments + heightBase) * quantizeScale
        - quantizeBias - standardHeight - 1
```

The required `quantizeBias`, `quantizeScale`, and `standardHeight` values come from `t3Terrain.xml`. So the terrain renderer should parse `t3Terrain.xml` before interpreting height values into world-space Z.

`t3TextureMasks` uses 4-bit values packed in byte nibbles and is organized into layer blocks. The number of layers can be derived from file size and dimensions, while the actual texture assigned to each mask is defined in `t3Terrain.xml`.

For pathing, SC2 Map Analyzer is a useful reality check: it historically approximated ground pathing from `t3CellFlags` when `t3SyncPathingInfo` was not decoded, then compared ground, cliff-walking, and air shortest paths. That implies:

- `t3CellFlags` can support early debug overlays.
- `t3SyncPathingInfo` should be treated as the target for real movement/building pathing.
- Footprints matter, and building pathing footprint rotation may differ from no-build footprint behavior.

## Implementation Plan

Near-term:

1. Replace fixture filename fallback with catalog-driven Unit -> Actor -> Model resolution.
2. Parse `Objects` documented attributes exactly, keeping fixture `x/y/z` as compatibility sugar.
3. Keep binary terrain readers (`t3HeightMap`, `t3SyncHeightMap`, `t3SyncCliffLevel`, `t3CellFlags`, `t3TextureMasks`) aligned with playable MapInfo bounds.
4. Expand `t3Terrain.xml` support beyond quantization and texture-layer metadata as needed.
5. Improve cliff/water/pathing reconstruction from source map data.
6. Add debug dumps that compare with sc2reader metadata and SC2 Map Analyzer-style pathing views.

Keep these boundaries:

- Map/archive parsing stays in `games/starcraft-2/common`.
- Model rendering stays in `games/starcraft-2/renderer`.
- Game spawning consumes resolved object records and registers normal model handles.
- Engine code only knows generic map paths, media handles, and entity state.
