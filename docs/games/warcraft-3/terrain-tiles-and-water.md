# WC3 Terrain Tile Atlases, Tileset Archives and Water

Renderer code: `games/warcraft-3/renderer/w3m/` (`r_war3map.c`, `r_war3map_ground.c`, `r_war3map_utils.c`,
`r_war3map_water.c`) and the SLK loaders in `games/warcraft-3/renderer/r_game.c`. Game side:
`G_ApplyTilesetWaterHeight` (`games/warcraft-3/game/g_main.c`) and `CM_W3SetWaterHeight`
(`games/warcraft-3/common/world_w3.c`).

## Ground tile atlases

`TerrainArt\Terrain.slk` `dir` + `file` names one BLP per ground tile. Each BLP is an atlas four cells tall:

| Shape | Layout | Example |
|-------|--------|---------|
| square (`width == height`) | 4x4 transition cells | `Lords_Dirt` 256x256, `Outland_Abyss` **64x64** |
| extended (`width > height`) | left 4x4 transitions, right 4x4 full-tile variations | `Outland_Dirt` 512x256 |

`SetTileUV` derives the cell size from the shape (`0.25` tall; `0.25` or `0.125` wide), never from a fixed
64-pixel cell. A 64-pixel assumption made the 64x64 Abyss atlas wrap whole per tile, which drew the black
checker pattern along HumanX04's plateau edges (issue #597). The extended half is chosen by
`groundVariation` 0-15; variation 16 selects transition cell 15, and anything higher selects cell 0.

## Tileset archives

`War3.mpq` and `War3x.mpq` each contain one nested archive per tileset: `A.mpq`, `L.mpq`, `O.mpq`, ... (ROC has
no I/J/K/O/Z). Retail opens `<tileset>.mpq` for the loaded map (the exe has a `%c.mpq` format string and no
`<tileset>_<file>` cliff path). It holds the tileset's `ReplaceableTextures` overrides:

| Archive | Contents (examples) |
|---------|---------------------|
| `O.mpq` (TFT) | `ReplaceableTextures\Cliff\Cliff0.blp`, `Cliff1.blp` (rough-dirt abyss cliffs), uber splats |
| `A.mpq` | `Cliff\cliff0/1.blp`, uber splats, `Water\Water00-44.blp` (Ashenvale water) |
| `L.mpq` | uber splats, `Units\Creeps\QuillBeast\QuillBeastBlue.blp` |

`R_W3OpenTilesetArchive(map->tileset)` keeps that archive open in memory for the map's lifetime. The renderer
core resolves textures and models through `R_AssetCandidates`: map import, then `R_GameAssetCandidate`
(`<tileset>.mpq\<path>`, returned only when the archive holds the file, so other lookups cost one hash probe),
then the base path. `CL_PrepRefresh` registers the map before configstring images, so building uber splats
also get the tileset variant.

The old `ReplaceableTextures\Cliff\A_Cliff0.blp`-style files in `War3.mpq` match ROC `A.mpq` but miss TFT
updates and every TFT tileset; without the archive layer, Outland abyss cliffs drew Lordaeron's grass `Cliff1`.

```bash
build/bin/mpqtool -mpq "data/Warcraft III/Frozen Throne/War3x.mpq" cat O.mpq > /tmp/O.mpq
build/bin/mpqtool -mpq /tmp/O.mpq cat "(listfile)"
```

## Cliff footprint ground

`R_MakeCliff` repaints the cliff footprint's corners: base-level corners take `CliffTypes.slk` `groundTile`,
higher corners take `upperTile` when set. For the abyss cliff `COrd` this keeps plateau rims rough dirt (`Osmb`,
as authored) instead of the opaque-black `Oaby` abyss atlas, whose full cell has alpha 255. Test:
`renderer_terrain.abyss_cliff_paints_low_corners_with_ground_and_high_corners_with_upper_tile`.

## Special terrain doodad geometry (#630)

Special `war3map.doo` placements store the minimum terrain cell, not a complete rigid model pose.
`pathTex` dimensions are four pixels per 128-unit cell. The game centers the entity in that footprint
and keeps its center terrain Z as the transport/collision anchor. The WC3 renderer retains each
placement in `r_war3map_special.c`, replaces generated cliffs over the **full** footprint, and builds
private fitted geometry when `R_RenderModel` draws the corresponding cliff-textured MDX instance.

For each transformed vertex, the world height is:

```
scaled native vertex Z + lowest footprint cliff layer * 128 - 256
    + bilinear raw W3E height at the vertex's world XY
```

Raw heights exclude interpolated cliff layers but include renderer terrain deformation, sampled through `R_GetVertexPosition`
so the fitted mesh uses the same height decode as the ground mesh. Using the center's single terrain Z
leaves the cave roof flat and disconnected; adding each vertex's interpolated cliff layer collapses
stacked lower wall vertices onto the roof. Keeping side/rear generated cliffs was an unsuccessful
workaround for this missing fitting step and must not return.

The fitted copy owns positions, normals, palettes, VAOs and GPU buffers; authored UV channels,
indices, materials and animation data remain borrowed from the registered model. Culling bounds
expand to the fitted Z range; mutable sprite/ribbon state is separate and released with the copy. The registered
model is never changed. A source/transform change rebuilds the copy. Terrain geometry rebuilds
invalidate fitted copies; clearing the map frees copies and placement metadata. Grid-corner normals
use the terrain normal, transformed back into the instance's coordinates.

Retail confirmation from the September 2002 demo `Game.dll` (SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`, base `0x6f000000`):

| Address | Contract |
|---|---|
| `0x6f129f30` | Footprint bounds are minimum XY through `minimum + path texture dimension / 4 - 1`. |
| `0x6f12a320` | Marks every footprint cell with the special-terrain flag; no side/rear inset. |
| `0x6f12a580` | Loads special doodad geometry, centers it using path dimensions, and calls the cliff fitting path `0x6f11db30` with normal updates enabled. |
| `0x6f11d440` | Finds minimum/maximum cliff layers across all footprint cells. |
| `0x6f11db30` | Restores authored vertices, rotates -90 degrees, applies minimum-layer baseline, then calls `0x6f11e200`. |
| `0x6f11e200`, `0x6f11e3e0` | Adds bilinear raw terrain height (`0x6f11ada0`, flags 4) per vertex; updates normals at terrain grid corners. |

The reported OrcX01 `YCr2` is at W3E cell `(12,160)`, footprint `3×3`, center `(-4416,12480)`.
Its center terrain Z is `257.5`; the rear native vertex `(-192,192,256)` must end at world
`(-4224,12672,528)`. The colocated native Z=0 wall vertex remains 256 units lower.
`wc3_terrain.orcx01_cave_vertices_follow_raw_height_and_keep_stacked_walls` embeds the 16 actual
W3E heights/layers and checks these joins, another interior height, shared-model isolation, both UV
channels, cache reuse, terrain invalidation, transport-anchor rebinding and the outer entity culler's fitted bounds. The full-footprint
regression also checks clipped/overflowing bounds. Both failed before the geometry fix.

Run `make test-renderer-model test-renderer-shadows` for the headless coverage. A bounded hidden
OrcX01 capture at that camera verifies the visible roof join; tests alone do not establish the silhouette.

## Cave material UV channels (#630)

The inspected TFT `NewRoughCliffDoodadCave2.mdx` has one 213-vertex geoset, `UVAS=2`, and two `UVBS`
arrays. Its material has an opaque cliff layer (`textureId=0`, replacement ID 11, `coordId=0`) and an
alpha-key cave overlay (`textureId=1`, `Textures\CaveCliffDoodad.blp`, `coordId=1`). The rear roof vertex
`(-192,64,256)` uses UV0 `(0.998047,0.001953)` and UV1 `(0.615494,0.979932)`.

`ReadGeoset` previously replaced `geoset->texcoord` on each UVBS, leaking UV0 and drawing both layers with
UV1. The cave-overlay coordinates near its bottom border sampled a narrow strip of the cliff texture,
producing the streaked roof in #630. Altering terrain cutouts or model elevation does not fix that material contract.

The loader now retains channels contiguously, each `num_texcoord` entries, and rejects mismatched/truncated
UVBS data. Channel zero remains in the packed model vertex buffer. Models with extra channels own one compact
secondary UV buffer; each geoset has one VAO per channel sharing positions, normals, skinning and indices.
`MDLX_RenderGeoset` selects the VAO by each layer's `coordId`. Invalid channel references are diagnosed and
skipped, rather than silently sampling UV0. Single-channel models retain the original packed-UV path and need
no secondary buffer. Model release deletes every channel VAO and the optional secondary stream.

`renderer_model.mdx_cave_uv_channels_do_not_overwrite_the_cliff_coordinates` drives the binary geoset parser,
GPU upload/binding construction (with only GL calls intercepted), channel selection and release. It also covers
single-channel data and rejection of a missing declared channel. Run `make test-renderer-model test-renderer-shadows`.
The fixture uses distinct UV0/UV1 values and needs no installed Warcraft archives. This UV contract also applies to
RoC two-layer cave models: the inspected base `CliffDoodadCave2.mdx` also declares `UVAS=2`.
See [doodad lifecycle](doodad-animation.md) and
[NightElfX04 ground-hole diagnosis](architecture/map-renderer.md#nightelfx04-isolated-holes-615).

For a framebuffer comparison, the local OrcX01 terrain placements include `YCr2` at `(12,160)` and
`LCc2` at `(8,104)`. Reveal the region and lift any scripted camera bounds before moving the camera: camera
movement alone leaves the cave under fog. When passing negative coordinates on the command line, put the whole
command in one argument, e.g. `'+camera move -4416 12480'`; separate `-4416` is parsed as a command-line switch.
Use `+screenshot 60 +com_frame_limit 80` for a bounded capture. Temporary diagnostic maps must retain every
archive entry: `mpqtool pack` creates a replacement archive, rather than appending one file to an existing archive.

## Water.slk per-tileset water

`TerrainArt\Water.slk` row `<tileset>Sha` (e.g. `LSha`, `OSha`):

| Column | Use |
|--------|-----|
| `height` | Surface offset in tiles: `W3_WaterSurfaceHeight` = `(waterlevel - 0x2000) / 4 + height * 128`. Shared by the game (`CM_GetWaterHeightAtPoint`, float/fly/amphibious support) and the renderer. Most tilesets `-0.7`, Outland `-1.5`. |
| `texFile`, `numTex` | Frames `texFile00.blp` ... resolved through the tileset archive layer. |
| `texRate` | Frames per second; `R_WaterFrame` picks `floor(time * texRate) mod numTex` from the renderer clock. |
| `Smin_*`/`Smax_*`, `Dmin_*`/`Dmax_*` | RGBA bands; `R_WaterDepthColor` interpolates shallow from 10/128 to 64/128 tiles of depth and deep from 64/128 to 72/128 (HiveWE/Warsmash band edges). |

Outland (`OSha`) is the Abyss: `ReplaceableTextures\TeamColor\TeamColor`, one frame, every band `0,0,0,255`
(opaque black), 1.5 tiles down. A missing row or zero `numTex` is logged; that map draws no water and the game
uses the raw W3E level.

```bash
build/bin/mpqtool -mpq "data/Warcraft III/Frozen Throne/War3x.mpq" cat TerrainArt/Water.slk
```

## Known gaps

- W3I `waterColor` (TFT custom water tint) is parsed into `mapInfo_t` but not applied.
- `Water.slk` `alphaMode`, `lighting` and shoreline (`shore*`) columns are not used.

## Tests

`make test-renderer-model`: `tile_atlas_cells_follow_texture_shape_not_pixel_size`,
`water_depth_color_follows_water_slk_bands`, `water_style_reads_tileset_row_from_water_slk`,
`water_frames_advance_at_texrate_and_wrap`, `tileset_archive_layers_between_map_imports_and_base_data`
(fixtures `games/warcraft-3/tests/resources-src/TerrainArt/Water.slk` and the generated `O.mpq` in
`build/tests/tests.mpq`). `make test-wc3-engine`: `wc3_movement.tileset_water_slk_height_places_the_water_surface`.
