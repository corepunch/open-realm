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

## UI texture resolution

SC2 layout files reference textures as logical `UI/` keys (`<Texture val="@UI/MenuBarButtonNormal"/>`). These keys are defined in `GameData/Assets.txt` inside each mod archive — the SC2 equivalent of WC3's `war3skins.txt`. See [assets-txt.md](file-formats/assets-txt.md) for the format.

`sc2_hud_image_index()` in `hud.c` resolves a key to a `gi.ImageIndex` handle using two tiers:

1. **Static `paths[]`** — hardcoded entries for Core.SC2Mod keys that the VFS cannot reach, because `Liberty.SC2Mod/Base.SC2Data` has higher archive priority and its `GameData/Assets.txt` shadows Core's. Covers ~30 HUD entries (menu bar, portrait, panels, etc.).

2. **Runtime `assets_catalog[]`** — parsed from `gi.ReadFile("GameData/Assets.txt")` at `SC2_HUD_InitLayoutHost()`. Returns the Liberty.SC2Mod version, which covers minimap buttons, autocast overlay, bordered white, and other Liberty-specific entries.

```c
/* Lookup order in sc2_hud_image_index() */
while (*resource == '@') resource++;   /* strip leading @ */
// 1. static paths[]
// 2. assets_catalog[] (from Assets.txt)
// 3. return 0 and log "unresolved" for any UI/ key still not found
```

**VFS priority note:** `FS_OpenFile` searches archives from last-loaded to first-loaded (index `MAX_ARCHIVES-1` → `0`). The archive load order in `g_sc2.c` puts `Liberty.SC2Mod/Base.SC2Data` at a higher index than `Core.SC2Mod/Base.SC2Data`, so `gi.ReadFile("GameData/Assets.txt")` always returns Liberty's copy. Core entries that Liberty does not repeat must live in the static table.

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

`SC2_LayoutFindFrameType()` iterates parsed `templates[]` comparing `sc2FrameType` enum values, then returns the corresponding flattened frame via `resolved_frame` pointer. Only templates that were visited during flattening (children of the `GameUI` root) have `resolved_frame != NULL`. Panel root frames like `ConsolePanel`, `ResourcePanel`, `CommandPanel`, and `InfoPanel` are children of the `GameUI` frame in `GameUI.SC2Layout` and are always flattened.

```c
sc2BaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type) {
    for (int i = 0; i < sc2_layout.num_templates; i++) {
        sc2Frame_t *tmpl = &sc2_layout.templates[i];
        if (tmpl->type == type && tmpl->resolved_frame)
            return tmpl->resolved_frame;
    }
    return NULL;
}
```

Templates store a direct pointer to their flattened frame via `resolved_frame`, avoiding index-based lookups between the two arrays. This contrasts with `SC2_LayoutFindFrameByName()` which iterates the flattened `frames[]` array directly.

## SC2 Image frames → FT_TEXTURE not FT_SPRITE

`SC2_FRAMETYPE_IMAGE` (the `<Frame type="Image">` SC2 element) maps to `FT_TEXTURE` in the engine, not `FT_SPRITE`. `FT_SPRITE` is reserved for `SC2_FRAMETYPE_MODEL` (3D scene models). `SCR_LayoutDrawTexture` handles `FT_TEXTURE` (2D images); `SCR_LayoutDrawSprite` handles `FT_SPRITE` (3D models via `re.DrawSprite`).

## Cross-panel anchor (ResourcePanel)

The ResourcePanel in `GameUI.SC2Layout` uses `<Anchor side="Right" pos="Min" relative="$parent/CashPanel"/>` — its right edge is anchored to the left edge of CashPanel. CashPanel is parsed from `CashPanel.SC2Layout` (loaded as a core file) and exists in the flattened frame tree. `SC2_ResolveNamedRelatives()` resolves this anchor to CashPanel's flat index at flatten time.

Do not override cross-panel anchors in game code. The layout data is authoritative; code must apply it faithfully.

## Template resolution: two-pass design

Template resolution runs in two passes inside `stb_sc2layout.h`:

**Pass 1 — per-file pass** (inside `SC2_ParseDescNode`): immediately after each file's top-level frames are parsed, the parser resolves templates for frames added by that file. This covers same-file templates and forward references from earlier-included files. On success, `template_path[0]` is cleared so the global pass won't re-resolve. On NOT FOUND (cross-file forward reference), `template_path` is left set for the global pass to retry.

**Pass 2 — global pass** (inside `SC2_LayoutBuildGameUI`): runs after all core files are loaded. Handles any remaining unresolved templates (forward references not caught per-file). Also clears `template_path[0]` on success. Logs a warning for templates still not found after all files are loaded.

The per-file pass without clearing caused doubling: per-file resolved and left `template_path` set; global pass then re-resolved, cloning children a second time. The fix is to clear `template_path[0]` in the per-file pass on success.

### File ordering constraint

`core_files[]` in `SC2_LayoutBuildGameUI` must be ordered leaf-to-root so that each template's dependencies are parsed before it is instantiated:

```
GameButton.SC2Layout        ← base button template (no deps)
CommandButton.SC2Layout     ← needs GameButton
PortraitPanel.SC2Layout     ← no game-file deps
MinimapPanel.SC2Layout      ← no game-file deps
ResourcePanel.SC2Layout     ← no game-file deps
CommandPanel.SC2Layout      ← needs CommandButton
ConsolePanel.SC2Layout      ← needs PortraitPanel
...
GameUI.SC2Layout            ← instantiates all panels (must be last)
```

Violating this order causes per-file pass "NOT FOUND" for cross-file refs, leaving them for the global pass. The global pass still handles them correctly, but it's cleaner and tests rely on per-file resolution working.

## SC2 button frames → FT_FRAME not FT_BUTTON

`SC2_FRAMETYPE_BUTTON` and `SC2_FRAMETYPE_COMMAND_BUTTON` map to `FT_FRAME`, not `FT_BUTTON`. SC2 buttons are containers — their visual appearance comes from child `NormalImage`/`HoverImage` frames (`FT_TEXTURE`). The client's `SCR_LayoutGlueTextButton` (called for `FT_BUTTON`) expects a `uiGlueTextButton_t` buffer that SC2 buttons don't carry; using `FT_FRAME` avoids the crash.

## BACKGROUND layer: only ConsolePanel + MinimapPanel

`hud_console.c` intentionally omits `CommandPanel` and `InfoPanel` from `LAYER_BACKGROUND`. Those panels are written by `hud_command.c` and `hud_infopanel.c` on their own dedicated layers. Writing them on `LAYER_BACKGROUND` would double-render them and bloat the background layer with 100+ command-card frames.

The `ConsoleUIContainer` frame is written as a bare container (no children) on `LAYER_BACKGROUND` so that children on other layers can use it as their parent reference.

## ConsolePanel Model children (3D console chrome)

`ConsolePanel.SC2Layout` contains three `<Frame type="Model">` children (`InfopanelModel`, `MinimapModel`, `CommandPanelModel`) that render the 3D console backdrop in the real SC2 engine. These map to `FT_SPRITE` but our `R_GameDrawSprite` for SC2 is a no-op (no .m3 model renderer). They are hidden in `sc2_hud_hide_optional_panels()` to prevent null-model draw calls.

## NormalImage / HoverImage button children (fill-parent semantics)

SC2 `GameButton` template defines `NormalImage` and `HoverImage` child Image frames with **no explicit anchors and no size**. In the real SC2 engine these implicitly fill the parent button bounds. In our renderer (`cl_layout.c::SCR_LayoutRect`):

- When a `FT_TEXTURE` frame has `size.width == 0 && size.height == 0` AND no both-side anchor pair to derive size from, we use the parent frame's rect dimensions as the element size.
- This correctly fills `NormalImage` / `HoverImage` to the enclosing button.

**Note:** `<DescFlags val="Internal"/>` on these frames is a metadata marker indicating they are internal template implementation details. `<CollapseLayout val="true"/>` on icon/image frames is a hint that they don't contribute to layout collapse — parsed and stored but not acted on by our renderer.

## Container height inference: pmax-only FT_FRAME panels

SC2 layout panels (`CommandPanel`, `InfoPanel`, etc.) commonly define only a **Bottom anchor + Width** without an explicit Height. The real SC2 engine derives height from the button grid. Our engine handles this in `SCR_InferContainerHeights` (`cl_layout.c`), called after each `SCR_Clear` frame-wire parse:

1. For every `FT_FRAME` with `size.height == 0` and **only** `pmax_y` set (Bottom-anchored, no Top/Mid):
2. Walk all descendants in the frame list; for each with a known `size.height`, compute its y-offset relative to the container top using `scr_frame_abs_y()` — a recursive anchor-chain walker that treats the container as y=0.
3. Take the maximum `(y_offset + child_height)` as the container's height.

This allows rows of buttons (row N anchors to `row(N-1).Max + gap`) to correctly determine CommandPanel's height (~230px for 3 rows of 76×76 buttons).

## Unresolved FT_TEXTURE frames (tex.index == 0)

When a SC2 Image frame's `@@UI/...` texture key is not found in `Assets.txt` or the static `paths[]` table, `gi.ImageIndex` returns 0. `SCR_LayoutDrawTexture` in `cl_scrn.c` skips drawing when `frame->tex.index == 0` to avoid drawing `cl.pics[0]` (the first registered image) as a visual artifact.
