# HUD Media Lifetime

## Contract

HUD FDF trees may stay cached for a level. **Texture and font configstring indices must not.** Names live for the process; indices live for the level.

`SV_Map` does `memset(&sv, 0, sizeof(sv))`, which empties `CS_IMAGES` and `CS_FONTS`. The game module is not unloaded, so a `static BOOL *_loaded` flag and a `FRAMEDEF.Texture.Image` slot from the previous map are still there. Serializing those slots after the wipe sends the old numbers into a new table that command-button `gi.ImageIndex(art)` fills from slot 1. `MAX_IMAGES` is 256, so chrome and command art collide. That is the save/load shuffled-icon bug.

Quake 2 does not parse UI files every frame. `G_SetStats` calls `gi.imageindex(item->icon)` from a **name** every frame, and `DrawPic` looks up that name. The one cached pic (`level.pic_health`) is assigned again in `SpawnEntities` after `memset(&sv)`. Same rule here.

## Data Flow

```text
G_LoadMap
  -> UI_ResetHud()
       scene *_loaded flags and binding structs
       UI_ClearTemplates()          // frames[], StringList, HUD name tables
  -> SpawnEntities / ImageIndex of unit art
  -> ClientBegin
       EnsureLoaded parses FDF once
       UI_LoadTexture / FontIndex record name -> new CS_IMAGES/CS_FONTS slot
  -> UI_Write* / UI_BuildFrameForWrite
       UI_LiveImage / UI_LiveFont re-ImageIndex from the name table
```

The HUD name table is write-once per slot until `UI_ResetHud`. After a wipe, `ImageIndex` reuses slot 1; overwriting that slot's remembered chrome name with the new command-button occupant is the same collision. Stale `FRAMEDEF` indices keep their original names; `UI_LiveImage` then `ImageIndex`es a fresh slot.

Do not parse ConsoleUI.fdf on every resource-bar write. Re-parse once per map. Isolated scene files (`hud_console.c`, `hud_infopanel.c`, …) stay; only media lifetime is centralized in `UI_ResetHud`.

Glue UI (`games/warcraft-3/ui/`) is a separate `stb_fdf` instance with its own `ui_textures[]`. It is not this contract.

## Client

Same-map load never changes `CS_WORLD`, so the client cannot wait for `CL_ClearState`. `CL_RestartRefresh` zeros `cl.pics` / `cl.fonts` / `cl.models` (renderer caches still own the GPU objects). `CL_PrepRefresh` always binds from the current configstring, and empty slots stay NULL so leftover art cannot draw.

## Diagnostic Workflow

After `load quick`, tabs must show resolved `KEY_QUESTS` strings and tab art, not the FDF ids themselves. Command-card icons must match the selected unit, not shuffled chrome.

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" -roc \
  +map "Maps/Campaign/Human02.w3m" +wait +save quick +wait +load quick \
  +screenshot 20 +com_frame_limit 80
```

Repeat with `-tft`. Engine coverage: `make test-wc3-engine WC3_PATTERN='wc3_game.hud_image*'`

## Known Pitfalls

- Copying `FRAMEDEF.Texture.Image` onto the wire without `UI_LiveImage` after a configstring wipe.
- Keeping `*_loaded` true across `G_LoadMap` while `frames[]` was cleared.
- Treating `GetConfigstring(CS_IMAGES + old_index)` as the name of a cached frame after the slot has been reused.
- Preserving `CS_IMAGES` across `SV_Map` as a workaround. The table is per-level.
