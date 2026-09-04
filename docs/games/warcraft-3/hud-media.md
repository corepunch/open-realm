# HUD Media Lifetime

## Contract

HUD FDF trees may stay cached for a level. **Texture and font configstring indices must not.** Names live for the process; indices live for the level.

`SV_Map` does `memset(&sv, 0, sizeof(sv))`, which empties `CS_IMAGES` and `CS_FONTS`. The game module is not unloaded. All in-game HUD bindings live in one BSS object, `hud`. `UI_ResetHud` is `memset(&hud, 0, sizeof(hud))` plus `UI_ClearTemplates()` for the FDF `frames[]` pool. Scene files bind into `hud.console`, `hud.simple`, and so on; a NULL root means "not bound yet".

Quake 2 does not parse UI files every frame. `G_SetStats` calls `gi.imageindex(item->icon)` from a **name** every frame, and `DrawPic` looks up that name. The one cached pic (`level.pic_health`) is assigned again in `SpawnEntities` after `memset(&sv)`. Same rule here.

## Data Flow

```text
G_LoadMap
  -> memset(&hud, 0, sizeof(hud))
  -> UI_ClearTemplates()
  -> UI_LoadHud()                   // every panel, once per level
  -> UI_Write* / UI_BuildFrameForWrite
       UI_LiveImage / UI_LiveFont re-ImageIndex from hud.image_key[]
```

`hud.image_key[]` is write-once per slot until the next memset. After a wipe, `ImageIndex` reuses slot 1; overwriting that slot's remembered chrome name with the new command-button occupant is the same collision.

Do not parse ConsoleUI.fdf on every resource-bar write. Isolated scene files stay; they share one accumulator.

Glue UI (`games/warcraft-3/menu/`) is a separate `stb_fdf` instance with its own `ui_textures[]`. It is not this contract.

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
- Leaving `hud` bindings live across `G_LoadMap` while `frames[]` was cleared.
- Treating `GetConfigstring(CS_IMAGES + old_index)` as the name of a cached frame after the slot has been reused.
- Preserving `CS_IMAGES` across `SV_Map` as a workaround. The table is per-level.
