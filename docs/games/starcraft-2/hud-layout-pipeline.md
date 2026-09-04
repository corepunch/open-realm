# SC2 In-Game HUD Layout Pipeline

Implements issue #82. Mirrors the WC3 server-authored HUD pattern.

## Selection/InfoPanel status

The InfoPanel subtree is structurally present on `TRaynor01` and now travels with the complete console tree in `LAYER_BACKGROUND`. Its native subtree includes `UnitPanel`, `UnitWireframe`, `ShieldLabel`, `LifeLabel`, `EnergyLabel`, `InfoPaneUnit`, and the latter's direct `NameLabel` child. Missing selected-unit presentation is therefore a binding and selection-lifecycle problem, not a missing layout message.

Do not establish an initial selection until native map-player assignment is parsed. Current runtime evidence is inconsistent across boundaries: `world_sc2.c` advertises map player 0, `SC2_InitClients` writes player 1, while controllable `TRaynor01` units are owned by native player 2. Inferring the human owner from the first mobile unit would also select civilians or neutral units on other maps.

When player mapping is available, selection changes should derive one primary selected edict, update the portrait model, `InfoPaneUnit/NameLabel`, stats, and commands, then resend the unified `LAYER_BACKGROUND` console tree. Life, shield, and energy labels must remain hidden until their authoritative game-state values exist.

## Overview

The server parses `.SC2Layout` XML files, produces a `sc2BaseFrame_t[]` array, stamps dynamic data (stats, text, visibility) onto frames, converts them to `uiFrame_t`, and sends via `svc_layout`. The client (`cl_unit_layout.c`) renders generically — it has no knowledge of SC2 layout files.

```
.SC2Layout files (MPQ / filesystem)
    │
    ▼
SC2_LayoutBuildGameUI()  [menu_layout.c]
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
| `games/starcraft-2/menu/menu_layout.c` + `.h` | Parser: XML → `sc2BaseFrame_t[]` |
| `games/starcraft-2/game/hud/hud.c` | Bridge: `sc2BaseFrame_t` → `uiFrame_t` + svc_layout framing |
| `games/starcraft-2/game/hud/hud_resource.c` | Resource panel (minerals/vespene/supply) |
| `games/starcraft-2/game/hud/hud_console.c` | Unified console tree (chrome, portrait, minimap, info, commands) |
| `games/starcraft-2/game/hud/hud_command.c` | Stamps command-card runtime data before console serialization |
| `common/shared.h` `UILAYOUTLAYER` | reuses `LAYER_CONSOLE/BACKGROUND/COMMANDBAR/INFOPANEL` |

## Send-on-connect pattern

HUD is sent once per client on connect (in `SC2_ClientBegin`), not every frame. The client retains the last received layout per layer and renders it each frame via `SCR_DrawLayout`. This mirrors WC3's approach where `G_RefreshResourceBar` caches resource values and only resends on change.

```c
/* g_sc2.c :: SC2_ClientBegin */
SC2_HUD_WriteResourcePanel(ent);
SC2_HUD_WriteConsolePanel(ent);
```

The `SC2_RunFrame` loop does NOT resend HUD. Selection changes will restamp dynamic command/info data and resend the unified console layer when that system is wired up.

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

`menu_layout.c` normally lives in `ui/` and links against `libmenu-sc2`. The game module can't link `libmenu-sc2` directly. Instead, `hud.c` `#include`s `menu_layout.c` directly (one extra translation unit in the unity build). A `uiImport_t uiimport` stub in `hud.c` bridges `gi.ReadFile`/`gi.MemFree` to the parser's file I/O. Renderer callbacks (`GetRenderer`, `GetTexture`) are left NULL — the parsing path never calls them.

```c
/* hud.c — file I/O shim for menu_layout.c */
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

The `UNITY` macro in `Makefile` only scans directories. Adding `menu_layout.c` to the GAME_SC2_LIB dependency list only adds it as a Make prerequisite, not to the compiled unity blob. The `#include "games/starcraft-2/menu/menu_layout.c"` in `hud.c` is intentional.

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

`SC2_HUD_EnsureLayout` returns only the parsed frame array. Missing or invalid `.SC2Layout` data is already diagnosed by the parser;
the HUD must remain absent so the content error is visible instead of being concealed by invented geometry:

```c
sc2BaseFrame_t *SC2_HUD_EnsureLayout(DWORD *count) {
    if (!layout_loaded) {
        layout_loaded = true;
        layout_ok = SC2_LayoutBuildGameUI();
    }
    if (layout_ok) return SC2_LayoutGetFrames(count);
    if (count) *count = 0;
    return NULL;
}
```

## Shorthand anchor: `<Anchor relative="$parent"/>`

SC2 layout files use a shorthand form with no `side`/`pos` attributes to mean "fill all four sides of the parent":

```xml
<Frame type="ConsolePanel" name="ConsolePanel" template="ConsolePanel/ConsolePanelTemplate">
    <Anchor relative="$parent"/>
</Frame>
```

The parser's `SC2_ParseAnchor()` in `menu_layout.c` handles this by expanding the shorthand into four anchors when `!side_str && !pos_str && relative`:

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

## DDX Schema Tables (stb_sc2layout.h)

`stb_sc2layout.h` parses SC2 layout XML through declarative descriptor tables:

- `sc2_frame_attrs[]`: Maps `<Frame>` XML attributes (`name`, `template`, `Image`) to `sc2Frame_t` struct offsets.
- `sc2_frame_fields[]`: Table-driven property parser mapping child XML tags (`Width`, `Height`, `Alpha`, `Visible`, `AcceptsMouse`, `CollapseLayout`, `HighlightOnHover`, `HighlightOnFocus`, `BatchImages`, `BatchText`, `Color`, `DescFlags`, `Projection`, `LayerCount`, `LayerVisible`, `TextureType`, `StateCount`) to frame struct offsets and flags via `sc2FrameFieldType_t` typed dispatch.
- `sc2_child_tags[]`: Dispatches structural child tags (`Anchor`, `Texture`, `Model`, `Camera`, `Frame`) to typed handler functions.
- `sc2_sides[]`, `sc2_positions[]`, `sc2_frame_types[]`: Table-driven lookups for anchor sides, anchor positions, and frame class strings.

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

## BACKGROUND layer: complete bottom console

`hud_console.c` writes the complete bottom console as one retained `LAYER_BACKGROUND` tree: `ConsolePanel`, `ConsoleUIContainer`, `MinimapPanel`, `InfoPanel`, and `CommandPanel`. This matches the native layout ownership and the WC3 console pattern. A layout message resets frame numbering, so splitting sibling panels across messages leaves their parent references pointing into unrelated retained trees.

Command data is stamped into the shared parsed tree before serialization. Do not add independent InfoPanel or CommandPanel layer writers; they would duplicate descendants and break shared parent ownership.

## Portrait panel (FT_PORTRAIT)

`PortraitPanel.SC2Layout` defines a `<Frame type="Portrait" name="Portrait">` child. This type maps to `FT_PORTRAIT` (added in `stb_sc2layout.h`: `SC2_FRAMETYPE_PORTRAIT` enum + `{ "Portrait", SC2_FRAMETYPE_PORTRAIT }` in the parse table).

`SCR_LayoutDrawPortrait` renders it via `re.RenderFrame` with `RDF_USE_ENTITY_CAMERA`. For SC2, `R_GameExtractEntityCamera` (`games/starcraft-2/renderer/r_game.c`) computes a bounds-based portrait camera from `m3->boundings` (BoundingSphere min/max/radius): 35° FOV perspective, camera at `center + (0, -dist*0.9, +radius*0.25)` looking at the bounding sphere center.

The portrait model index is set server-side by `SC2_HUD_SetPortraitModel(model)` (hud.c) before `SC2_HUD_WriteConsolePanel`. In `SC2_ClientBegin` (g_sc2.c), the first selectable unit's `s.model` is used. `SC2_HUD_BuildFrameForWrite` overrides `tex.index` with `portrait_model` for any `FT_PORTRAIT` frame.

The `PortraitPanel` background image (`UI/BlankPortraitBackground` → `terranblankportrait_static.dds`) is a DXT5 texture that is black by design — it is the blank state shown behind the 3D portrait model.

## ConsolePanel Model children (3D console chrome)

`ConsolePanel.SC2Layout` contains three `<Frame type="Model">` children — `InfopanelModel`, `MinimapModel`, `CommandPanelModel` — referencing `.m3` models from `Assets.txt`:

| Frame | XML Position | Asset key | .m3 path |
|-------|-------------|-----------|----------|
| MinimapModel     | X=-1, Y=-1 | `UI/ConsoleModelMinimapPanel`  | `Assets/UI/Console/Terran/ConsoleTerran/ConsoleTerran_00.m3` |
| InfopanelModel   | X=0, Y=-1  | `UI/ConsoleModelInfopanel`     | `ConsoleTerran_01.m3` |
| CommandPanelModel| X=+1, Y=-1 | `UI/ConsoleModelCommandPanel`  | `ConsoleTerran_02.m3` |

The model frames map to `FT_PORTRAIT`, with model handles resolved from the original `Assets.txt`.
`SC2_ParseModel` retains Position/Scale; `SC2_ParseCamera` retains eye/target/FOV/clip planes, and Projection
selects an enum. Template inheritance tracks eight presence bits, preserving explicit zero overrides.
`SC2_HUD_BuildFrameForWrite` sends this `UIMODEL` through the existing `uiFrame_t.buffer` blob. Neither
`entityState_t` nor `playerState_t` changes. Incomplete model cameras are reported by `SC2_HUD`.

`SCR_LayoutDrawPortrait` uses `UI_ModelMatrix` for these payloads; ordinary unit portraits retain their
bounds-derived camera. The layout camera uses normalized viewport Position: X anchors the left/center/right
edge, Y anchors the bottom, and Z is model depth. Native SC2 console assets use X=-1/0/+1, Y=-1, scale=1,
eye=(0,-5,0), target=(0,0,0), FOV=90, near=1, far=1000. The three chrome models use the final frame of
their `Birth` sequence as the stable assembled pose. In particular, `ConsoleTerran_01.m3` moves its parent
bone from Z=-0.414 to Z=+0.007 during `Birth`; its `Stand` sequences omit that placement track and reset to
the hidden bind pose, leaving the center console absent. At the authored 4:3 aspect, the orthographic
half-width is tan(FOV/2), half-height is half-width/(4/3). Widening changes only half-width; model dimensions
remain proportional to screen height. The `Stand` info-panel mesh can sit below the bottom edge in its
unselected state; do not recenter it from its bounding sphere.

The previous code discarded camera/scale/Y/Z, inferred orthographic mode from the animation name `Stand`,
and framed each chrome piece using its different bounding radius. Runtime bounds were approximately
0.54/0.85/0.94, which explains the inconsistent, oversized pieces. Do not restore radius framing, crop the
full-screen layout viewport, or insert per-panel anchor overrides. `RDF_NOWORLDMODEL` prevents these
layout camera draws from running a world shadow pass.

Verification: `make test-sc2` covers camera parsing, inheritance, explicit zeroes, both projections and
4:3/16:9 anchor/scale invariants. Capture both aspect ratios with `+vid_mode 5` and `+vid_mode 4`:

```sh
build/bin/opensc2 -data data/StarCraft2 +vid_mode 4 +map TRaynor01 +screenshot 5 +com_frame_limit 10
build/bin/opensc2 -data data/StarCraft2 +vid_mode 5 +map Maps/TerranTest.SC2Components +screenshot 5 +com_frame_limit 10
```

The cvar is `com_frame_limit`, not `com_framelimit`. This branch writes JPEG screenshots. Pink placeholders
for missing fixture minimap/portrait assets are separate from console geometry; the campaign minimap loads.
See [shadow and catalog diagnostics](map-model-unit-data.md#shadow-and-catalog-diagnostics).
