# SC2 In-Game HUD Layout Pipeline

Implements issue #82. Mirrors the WC3 server-authored HUD pattern.

## Selected-unit presentation (September 2026)

The game owns selection, command authority and presentation. Empty selection has no
unit art or command card. Selected units bind their CUnit/CActorUnit name, wireframe,
life/energy/shields, armor and kills. Foreign units can be inspected, but only owned,
alive, unpaused units with enabled abilities receive commands. TRaynor01's user is
player 1; dropship cargo inherits the transport owner.

CUnit.CardLayouts supplies row/column/AbilCmd/Face; inherited CButton entries supply
art, localized labels and hotkeys. Move, Stop, Hold Position, Patrol and Attack route
to game-owned ability procedures. Unsupported commands stay unavailable. Raynor01
has no inventory ability in its catalog: the native InventoryPanel remains hidden.
This does not implement inventory behavior for other SC2 units.

The portrait currently uses the authored CModel.Image static mode. Animated FXA
portraits require RequiredAnims (.m3a) and FacialController playback, which the
renderer does not yet implement. Do not substitute a guessed model or texture.

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

## Updates and media

SC2_ClientBegin sends the console and resource layers. SC2_HUD_Update compares
selected-unit state, selection count, UI mode, minimap presentation and supply each
simulation frame, then resends changed presentation. Resource values use stat
bindings. Map changes clear layout, localization and wire-number caches.

UI texture aliases resolve through merged Core/Liberty/map Assets.txt layers.
GameStrings.txt and GameHotkeys.txt follow the same data layering. CButton icons
are real retail DDS files. Some single-level compressed DDS files leave
`dwMipMapCount` zero and omit DDSD_MIPMAPCOUNT: that means one level. The shared
DDS loader previously uploaded no level for those icons, producing missing art.

## Native layout semantics

Templates instantiate independent child trees. Named overrides replace properties
in their scoped subtree; relative paths resolve `$parent`, repeated parents and
root-qualified names without a global same-name fallback. Resolve referenced
definitions before copying inherited children. StateCount splits texture atlas UVs,
so minimap buttons show one state rather than a stacked normal/hover atlas.
Opposing MID anchors with explicit dimensions define a centered image rather than
a zero-sized rectangle. Hidden containers retain transparent geometry carriers so
visible siblings keep their native anchor chains.

SC2 uses a 1600×1200 canvas expanded horizontally for widescreen. Wire offsets use
`32767 / 1600`, not normalized WC3 offsets. Reserve ancestor and visible-subtree
numbers before writing frames: anchors may reference a later sibling. Layout
indices and wire numbers have different limits; diagnose wire exhaustion rather
than emitting frame zero. Console model backgrounds draw before portrait content.

Minimap Ping, terrain visibility and alliance colors are game-authored actions.
The renderer reads the opaque presentation flags and snapshot contact metadata.
Minimap contacts use the pixel canvas dimensions.

## Layout parser in the game module

`menu_layout.c` normally lives in `menu/` and links against `libmenu-sc2`. The SC2 HUD is server-authored, so the game module includes the layout parser directly as one extra translation unit in the unity build. `hud.c` supplies a `sc2LayoutImport_t sc2_layout_import` host table for file I/O and server archive indexes; there are no menu-module or renderer callbacks in this path.

```c
/* hud.c — file I/O shim for menu_layout.c */
static int sc2_hud_read_file(LPCSTR filename, void **buf) {
    DWORD size = 0;
    *buf = gi.ReadFile(filename, &size);
    return *buf ? (int)size : -1;
}
sc2LayoutImport_t sc2_layout_import;
void SC2_HUD_InitLayoutHost(void) {
    sc2_layout_import.FS_ReadFile = sc2_hud_read_file;
    sc2_layout_import.FS_FreeFile = (void (*)(void *))gi.MemFree;
}
```

`SC2_HUD_InitLayoutHost()` is called from `SC2_Init()` in `g_sc2.c`.

## Unity build note

The `UNITY` macro in `Makefile` only scans directories. Adding `menu_layout.c` to the GAME_SC2_LIB dependency list only adds it as a Make prerequisite, not to the compiled unity blob. The `#include "games/starcraft-2/menu/menu_layout.c"` in `hud.c` is intentional.

## Anchor conversion: sc2BaseFramePoint_t → uiFramePoint_t

SC2 anchors use `SC2_SIDE_{LEFT,RIGHT,TOP,BOTTOM}` + `SC2_POS_{MIN,MID,MAX}` mapped to the flat `sc2BaseFramePoints_t x[FPP_COUNT], y[FPP_COUNT]` arrays. These map directly to `uiFramePoint_t` with:
- `targetPos` = `FPP_MIN/MID/MAX` from `sc2BaseFramePoint_t.targetPos`
- `relativeTo` = wire frame number looked up from `relative_index` (or `UI_PARENT` when `-1`)
- `offset` = `int16_t(px->offset * UI_FRAMEPOINT_SCALE)` where `UI_FRAMEPOINT_SCALE = 32767.0 / 1600.0`

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

The parser maps `SC2_FRAMETYPE_BUTTON` and unbound `SC2_FRAMETYPE_COMMAND_BUTTON` to `FT_FRAME`, not `FT_BUTTON`. Bound gameplay cards become `FT_COMMANDBUTTON` with the proper `uiCommandButton_t` payload. SC2 buttons are containers — their visual appearance comes from child `NormalImage`/`HoverImage` frames (`FT_TEXTURE`). The client's `SCR_LayoutGlueTextButton` (called for `FT_BUTTON`) expects a `uiGlueTextButton_t` buffer that SC2 buttons don't carry; using `FT_FRAME` avoids the crash.

## BACKGROUND layer: complete bottom console

`hud_console.c` writes the complete bottom console as one retained `LAYER_BACKGROUND` tree: `ConsolePanel`, `ConsoleUIContainer`, `MinimapPanel`, `InfoPanel`, and `CommandPanel`. This matches the native layout ownership and the WC3 console pattern. A layout message resets frame numbering, so splitting sibling panels across messages leaves their parent references pointing into unrelated retained trees.

Command data is stamped into the shared parsed tree before serialization. Do not add independent InfoPanel or CommandPanel layer writers; they would duplicate descendants and break shared parent ownership.

## Portrait panel

The parser preserves the native Portrait frame. Selected-unit presentation changes
it to FT_TEXTURE using the inherited CModel.Image static portrait. Empty selection
hides the panel. Animated portraits remain a separate renderer feature: their
FXA RequiredAnims and FacialController playback are not implemented.

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

`SCR_LayoutDrawPortrait` uses `M_ModelMatrix` for these payloads; ordinary unit portraits retain their
bounds-derived camera. The layout camera uses normalized viewport Position: X anchors the left/center/right
edge, Y anchors the bottom, and Z is model depth. Native SC2 console assets use X=-1/0/+1, Y=-1, scale=1,
eye=(0,-5,0), target=(0,0,0), FOV=90, near=1, far=1000. The three chrome models use the final frame of
their `Birth` sequence as the stable assembled pose. In particular, `ConsoleTerran_01.m3` `Birth` (0..1333)
moves bone `ConsoleTerran_01~DUP~0` from Z=-0.414 to Z=+0.007; the last in-sequence sample is frame 1332.
`Stand` holds that same assembled translation, but other steady sequences omit the placement track and fall
back to the bind pose, which sits below the ortho bottom. Do not switch the payload back to frame 0.

These draws set `RF_PORTRAIT_LIGHTING` and must keep native M3 axes (`sc2_native_basis`). Commit `15480ab8`
(2026-09-24) started applying `sc2_model_basis`, a +90° yaw that aligns a unit's -Y front with world heading.
The layout camera is not that heading: it looks along +Y with +Z up, and was matched to an identity model
matrix. The yaw turns the wide console edge-on, so the assembled pose reads as the opening frame shoved to
the side. World actors still use `sc2_model_basis`. Unit portraits use the same lighting flag; their camera
is extracted from the same matrix, so identity preserves the pre-change portrait framing. At the authored 4:3 aspect, the orthographic
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

See [Galaxy presentation state](galaxy-presentation.md) for objective/help metadata ownership and the remaining path from script records to HUD output.

## Verified TRaynor01 HUD

September 30, 2026: retail NativeLib/Core/Liberty layout and catalog data resolved
Jim Raynor's five basic commands, static portrait, wireframe, life and armor.
Move and Stop were clicked in the running game and verified with `cmd unitinfo`:
Move set order 1/moving 1 and changed position; Stop set order 2/moving 0.
Patrol set order 4/moving 1, Hold Position set order 3/moving 0, and Attack
move through the minimap set order 5/moving 1 and cleared targeting.
Selecting a Marine updated the HUD to 45/45 life. Terrain and alliance-color
buttons changed the minimap, with visible contacts and the camera footprint.
Ping was exercised in the running game. Solid fills now use the normal UI batch
path, resetting the model transform and using the active canvas; previously a
ping could reuse console-model state and cover the screen with a white rectangle.

The shared minimap projector now reads R_WorldOrigin and R_WorldSize from the game
renderer. Requiring the WC3 `tr.world` object rejected SC2 projection and inverse
input even with a valid SC2 map. WC3 retains its existing center and dimensions.

Screenshots are saved locally under `screenshots/sc2-hud-*.png`. Automated checks:
`make test-sc2` (885 assertions), `make test-sc2-engine` (623), `make test-galaxy`
(173), the full `make test`, and `tools/engine_boundary_audit.py`.
