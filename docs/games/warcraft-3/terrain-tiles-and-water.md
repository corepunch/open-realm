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
