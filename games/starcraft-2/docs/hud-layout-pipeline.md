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
cl_unit_layout.c renders generically (SCR_Clear → SCR_LayoutDrawOverlay)
```

## Key files

| File | Role |
|------|------|
| `games/starcraft-2/ui/sc2_layout.c` + `.h` | Parser: XML → `sc2BaseFrame_t[]` |
| `games/starcraft-2/game/hud/hud.c` | Bridge: `sc2BaseFrame_t` → `uiFrame_t` + fallback frames + svc_layout framing |
| `games/starcraft-2/game/hud/hud_resource.c` | Resource panel (minerals/vespene/supply) |
| `games/starcraft-2/game/hud/hud_console.c` | Console panel (menu bar, chat) |
| `games/starcraft-2/game/hud/hud_command.c` | Command card (ability buttons) |
| `games/starcraft-2/game/hud/hud_infopanel.c` | Info panel (selected unit stats) |
| `common/shared.h` `UILAYOUTLAYER` | reuses `LAYER_CONSOLE/BACKGROUND/COMMANDBAR/INFOPANEL` |

## Send-on-connect pattern

HUD is sent once per client on connect (in `SC2_ClientBegin`), not every frame. The client retains the last received layout per layer and renders it each frame via `SCR_DrawLayout`. This mirrors WC3's approach where `G_RefreshResourceBar` caches resource values and only resends on change.

```c
/* g_sc2.c :: SC2_ClientBegin */
SC2_HUD_WriteResourcePanel(ent);
SC2_HUD_WriteConsolePanel(ent);
SC2_HUD_WriteCommandPanel(ent);
SC2_HUD_WriteInfoPanel(ent);
```

The `SC2_RunFrame` loop does NOT resend HUD — static panels never change, and dynamic panels (command/info) will be sent on selection change when that system is wired up.

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

## Dedicated-server wire diagnostics

Run a bounded headless session with `+sv_debug_layout 1` to summarize each `svc_layout` immediately before the server
puts it on the client netchan:

```sh
build/bin/opensc2 -data data/StarCraft2 +dedicated 1 +map TRaynor01 \
  +set sv_debug_layout 1 +com_frame_limit 3
```

Each `SV layout` line reports the layer, encoded byte count, frame count, nonzero texture handles, and live stat bindings.
`frames > 0` with `textured=0 stats=0` means the game sent a structurally valid but visually empty HUD.

## Dynamic stat bindings (SC2 → engine stats)

| SC2 concept | Engine `PLAYERSTATE_*` |
|-------------|------------------------|
| Minerals | `PLAYERSTATE_RESOURCE_GOLD` |
| Vespene gas | `PLAYERSTATE_RESOURCE_LUMBER` |
| Supply used | `PLAYERSTATE_RESOURCE_FOOD_USED` |

## Shared layout load (one call for all panels)

All panel writers call `SC2_HUD_EnsureLayout()` which loads `SC2_LayoutBuildGameUI()` exactly once. Previously each panel had its own `*_ensure_loaded` guard that would `SC2_LayoutInit()` and wipe the previous load — each panel would get an empty frame array. The shared load is assigned from `SC2_Init` before clients connect:

```c
/* g_sc2.c :: SC2_Init */
SC2_HUD_EnsureLayout(NULL);
```

`SC2_HUD_EnsureLayout` returns the frame array or a programmatic fallback when the XML layout can't be parsed (no SC2 data available):

```c
sc2BaseFrame_t *SC2_HUD_EnsureLayout(DWORD *count) {
    if (!layout_loaded) {
        layout_loaded = true;
        layout_ok = SC2_LayoutBuildGameUI();
    }
    if (layout_ok) {
        if (count) *count = (DWORD)sc2_layout.num_frames;
        return sc2_layout.frames;
    }
    /* Fallback when SC2 data is unavailable */
    if (SC2_HUD_BuildFallbackLayout()) {
        if (count) *count = sc2_fb_count;
        return sc2_fb_frames;
    }
    if (count) *count = 0;
    return NULL;
}
```

## Programmatic fallback frames

When `SC2_LayoutBuildGameUI()` fails (SC2 data missing), `SC2_HUD_BuildFallbackLayout()` builds a hardcoded `sc2BaseFrame_t[]` with:

| Index | Frame | Type | Parent |
|-------|-------|------|--------|
| 0 | GameUI root | FT_FRAME | −1 (scene) |
| 1 | ConsolePanel | FT_FRAME | 0 |
| 2 | Console background texture | FT_SPRITE | 1 |
| 3 | Minimap | FT_MINIMAP | 1 |
| 4 | ResourcePanel | FT_FRAME | 0 |
| 5 | Gold label | FT_TEXT | 4 |
| 6 | Vespene label | FT_TEXT | 4 |
| 7 | Supply label | FT_TEXT | 4 |

Panel writers try `SC2_LayoutFindFrameByType()` first, falling back to `SC2_HUD_FindFallbackFrameByType()` which walks the fallback array by mapped `FRAMETYPE`.

## Shorthand anchor: `<Anchor relative="$parent"/>`

SC2 layout files use a shorthand form with no `side`/`pos` attributes to mean "fill all four sides of the parent":

```xml
<Frame type="ConsolePanel" name="ConsolePanel" template="ConsolePanel/ConsolePanelTemplate">
    <Anchor relative="$parent"/>
</Frame>
```

The parser's `SC2_ParseAnchor()` in `sc2_layout.c` handles this by expanding the shorthand into four anchors when `!side_str && !pos_str && relative`:

| Side | Pos |
|------|-----|
| Top | Min |
| Bottom | Max |
| Left | Min |
| Right | Max |

Before this fix, missing `side`/`pos` was treated as malformed input and the parser returned without adding any anchors — every panel root frame had a computed rect of `(0,0,0,0)` and was invisible.

## Frame lookup by SC2 type

`SC2_LayoutFindFrameType()` iterates parsed `templates[]` comparing `sc2FrameType` enum values, then returns the corresponding flattened frame via `resolved_index`. Only templates that were visited during flattening (children of the `GameUI` root) have `resolved_index >= 0`. Panel root frames like `ConsolePanel`, `ResourcePanel`, `CommandPanel`, and `InfoPanel` are children of the `GameUI` frame in `GameUI.SC2Layout` and are always flattened.

```c
sc2BaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type) {
    for (int i = 0; i < sc2_layout.num_templates; i++) {
        sc2Frame_t *tmpl = &sc2_layout.templates[i];
        if (tmpl->type == type && tmpl->resolved_index >= 0)
            return &sc2_layout.frames[tmpl->resolved_index];
    }
    return NULL;
}
```

Because templates and frames are in separate arrays, a two-step lookup is needed: find the template by type, then use its `resolved_index` to access the flattened frame. This contrasts with `SC2_LayoutFindFrameByName()` which iterates the flattened `frames[]` array directly.

## SC2 Image frames → FT_TEXTURE not FT_SPRITE

`SC2_FRAMETYPE_IMAGE` (the `<Frame type="Image">` SC2 element) maps to `FT_TEXTURE` in the engine, not `FT_SPRITE`. `FT_SPRITE` is reserved for `SC2_FRAMETYPE_MODEL` (3D scene models). `SCR_LayoutDrawTexture` handles `FT_TEXTURE` (2D images); `SCR_LayoutDrawSprite` handles `FT_SPRITE` (3D models via `re.DrawSprite`).

## Cross-panel anchor (ResourcePanel)

The ResourcePanel in `GameUI.SC2Layout` uses `<Anchor side="Right" pos="Min" relative="$parent/CashPanel"/>` — its right edge is anchored to the left edge of CashPanel. CashPanel is parsed from `CashPanel.SC2Layout` (loaded as a core file) and exists in the flattened frame tree. `SC2_ResolveNamedRelatives()` resolves this anchor to CashPanel's flat index at flatten time.

Do not override cross-panel anchors in game code. The layout data is authoritative; code must apply it faithfully.
