# SC2 In-Game HUD Layout Pipeline

Implements issue #82. Mirrors the WC3 server-authored HUD pattern.

## Overview

The server parses `.SC2Layout` XML files, produces a `sc2BaseFrame_t[]` array, stamps dynamic data (stats, text, visibility) onto frames, converts them to `uiFrame_t`, and sends via `svc_layout`. The client (`cl_unit_layout.c`) renders generically — it has no knowledge of SC2 layout files.

```
.SC2Layout files (MPQ / filesystem)
    │
    ▼
SC2_LayoutBuildGameUI()  [sc2_layout.c]
    │  → sc2BaseFrame_t[] (positions, hierarchy, anchors, types resolved)
    ▼
Game code stamps dynamic data
    │  .stat, .text, SC2_UIFLAG_HIDDEN, etc.
    ▼
SC2_HUD_BuildFrameForWrite()  [game/hud/hud.c]
    │  → uiFrame_t (anchor→uiFramePoint_t, parent index, color, size, tex)
    ▼
gi.Write(PF_UIFRAME, &frame) × N  [svc_layout wire format]
    │
    ▼
cl_unit_layout.c renders generically
```

## Key files

| File | Role |
|------|------|
| `games/starcraft-2/ui/sc2_layout.c` + `.h` | Parser: XML → `sc2BaseFrame_t[]` |
| `games/starcraft-2/game/hud/hud.c` | Bridge: `sc2BaseFrame_t` → `uiFrame_t` + svc_layout framing |
| `games/starcraft-2/game/hud/hud_resource.c` | Resource panel (minerals/vespene/supply) |
| `games/starcraft-2/game/hud/hud_console.c` | Console panel (menu bar, chat) |
| `games/starcraft-2/game/hud/hud_command.c` | Command card (ability buttons) |
| `games/starcraft-2/game/hud/hud_infopanel.c` | Info panel (selected unit stats) |
| `common/shared.h` `UILAYOUTLAYER` | reuses `LAYER_CONSOLE/BACKGROUND/COMMANDBAR/INFOPANEL` |

## Layout parser in the game module

`sc2_layout.c` normally lives in `ui/` and links against `libui-sc2`. The game module can't link `libui-sc2` directly. Instead, `hud.c` `#include`s `sc2_layout.c` directly (one extra translation unit in the unity build). A `uiImport_t uiimport` stub in `hud.c` bridges `gi.ReadFile`/`gi.MemFree` to the parser's file I/O. Renderer callbacks (`GetRenderer`, `GetTexture`) are left NULL — the parsing path never calls them.

```c
/* hud.c — file I/O shim for sc2_layout.c */
static int sc2_hud_read_file(LPCSTR filename, void **buf) {
    DWORD size = 0;
    *buf = gi.ReadFile(filename, &size);
    return *buf ? (int)size : -1;
}
uiImport_t uiimport;
void SC2_HUD_InitLayoutHost(void) {
    uiimport.FS_ReadFile = sc2_hud_read_file;
    uiimport.FS_FreeFile = (void (*)(void *))gi.MemFree;
}
```

`SC2_HUD_InitLayoutHost()` is called from `SC2_Init()` in `g_sc2.c`.

## Unity build note

The `UNITY` macro in `Makefile` only scans directories. Adding `sc2_layout.c` to the GAME_SC2_LIB dependency list only adds it as a Make prerequisite, not to the compiled unity blob. The `#include "games/starcraft-2/ui/sc2_layout.c"` in `hud.c` is intentional.

## Anchor conversion: sc2BaseFramePoint_t → uiFramePoint_t

SC2 anchors use `SC2_SIDE_{LEFT,RIGHT,TOP,BOTTOM}` + `SC2_POS_{MIN,MID,MAX}` mapped to the flat `sc2BaseFramePoints_t x[FPP_COUNT], y[FPP_COUNT]` arrays. These map directly to `uiFramePoint_t` with:
- `targetPos` = `FPP_MIN/MID/MAX` from `sc2BaseFramePoint_t.targetPos`
- `relativeTo` = wire frame number looked up from `relative_index` (or `UI_PARENT` when `-1`)
- `offset` = `int16_t(px->offset * UI_FRAMEPOINT_SCALE)` where `UI_FRAMEPOINT_SCALE = 32767.0`

## Frame numbering

Wire frame numbers are assigned sequentially as frames are written in a given layer (reset per `SC2_HUD_WriteStart`). `parent_index == (DWORD)-1` means root; the wire `parent` field is 0.

## Dynamic stat bindings (SC2 → engine stats)

| SC2 concept | Engine `PLAYERSTATE_*` |
|-------------|------------------------|
| Minerals | `PLAYERSTATE_RESOURCE_GOLD` |
| Vespene gas | `PLAYERSTATE_RESOURCE_LUMBER` |
| Supply used | `PLAYERSTATE_RESOURCE_FOOD_USED` |

## Per-panel ensure-loaded pattern

Each panel module has a static `*_ensure_loaded()` guard:
```c
static BOOL resource_loaded;
static sc2BaseFrame_t *resource_root;

static void resource_ensure_loaded(void) {
    if (resource_loaded) return;
    resource_loaded = true;
    SC2_LayoutInit();
    SC2_LayoutBuildGameUI();
    resource_root  = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    SC2_LayoutGetFrames(&resource_count);
}
```

Because these are in the same unity translation unit, all static function names must be unique across panel files (`resource_ensure_loaded`, `console_ensure_loaded`, etc.).
