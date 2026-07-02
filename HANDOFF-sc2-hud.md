# SC2 HUD Resource Bar — Handoff Document

## Goal

Get the SC2 in-game resource bar (minerals, vespene, supply) rendering correctly on screen. The resource bar is a cross-panel UI element defined in StarCraft II `.SC2Layout` files and positioned via anchor resolution.

## What We've Done

### 1. Layout Parsing (`stb_sc2layout.h`)

Consolidated all SC2 layout parsing into a single-header library (`games/starcraft-2/common/stb_sc2layout.h`). The layout is fully parsed from `.SC2Layout` files — 438 frames built from 831 templates, 78 constants.

Key pipeline:
```
SC2_LayoutParseFile() → SC2_LayoutBuildGameUI() → SC2_FlattenFrame() → sc2BaseFrame_t[]
```

### 2. Cross-File Template Resolution

Added a global template-resolution pass after all files are loaded, to resolve templates that reference templates in other files (e.g., `GameUI.SC2Layout` references `CashPanel` template from `CashPanel.SC2Layout`).

### 3. Anchor Resolution Refactoring (3 phases)

Replaced all index-based lookups with pointer-based and flat-array approaches:

- **Phase 1**: `sc2Frame_t.resolved_index` (int) → `sc2BaseFrame_t *resolved_frame` (pointer). `SC2_ResolveAnchors` now uses `src->parent` directly instead of searching templates by index.

- **Phase 2**: `assign_number`/`lookup_number` → flat `frame_to_wire[]` map with `(DWORD)-1` sentinel for unassigned. `get_wire(index)` returns wire number or 0 for unassigned.

- **Phase 3**: `(DWORD)-2` sentinel → `relative_name` field on `sc2BaseFramePoint_t`. Named relatives store resolved name pointer; `SC2_ResolveNamedRelatives` searches `frames[m].name` directly.

### 4. Cross-Panel Anchor Fix (`hud_resource.c`)

Changed `SC2_HUD_WriteResourcePanel` to write the **full GameUI tree** (frame 0) on `LAYER_CONSOLE` instead of just the ResourcePanel subtree. This ensures CashPanel gets a wire number before ResourcePanel references it.

**Before**: `SC2_HUD_WriteLayout(ent, frames, count, root, LAYER_CONSOLE)` — writes only ResourcePanel subtree → CashPanel not in wire set → `get_wire(280)` returns 0 (= UI_PARENT) → anchor points to scene root → ResourcePanel positioned off-screen.

**After**: `SC2_HUD_WriteLayout(ent, frames, count, &frames[0], LAYER_CONSOLE)` — writes full tree → CashPanel gets wire #9 → ResourcePanel's Right anchor resolves to CashPanel's left edge −15px.

### 5. Server-to-Client Write Path

```
hud_resource.c  → SC2_HUD_WriteResourcePanel(ent)
                   → SC2_HUD_WriteLayout(ent, frames, count, &frames[0], LAYER_CONSOLE)
                     → SC2_HUD_WriteStart(LAYER_CONSOLE)
                     → SC2_HUD_WriteFrameWithChildren(frames, count, &frames[0])
                       → SC2_HUD_WriteFrame(frame) — for each visible frame
                         → SC2_HUD_BuildFrameForWrite(frame, &tmp) — converts sc2BaseFrame_t → uiFrame_t
                         → gi.Write(PF_UIFRAME, &tmp)
                     → SC2_HUD_WriteEnd(ent) — terminator
```

### 6. Stat Bindings

`resource_find()` in `hud_resource.c` binds labels to player state slots:
- `ResourceLabel0` → `PLAYERSTATE_RESOURCE_GOLD` (minerals)
- `ResourceLabel1` → `PLAYERSTATE_RESOURCE_LUMBER` (vespene)
- `ResourceLabel2` → `PLAYERSTATE_RESOURCE_HERO_TOKENS`
- `SupplyLabel` → `PLAYERSTATE_RESOURCE_FOOD_USED`

## Current State — What's NOT Working

### Resource Bar Not Visible on Screen

The layout builds correctly (438 frames, 831 templates), the cross-panel anchor to CashPanel resolves correctly (`relative_index = 280`, wire number = 9), but the resource bar does not render on screen. Some console UI elements appear as white squares (placeholder textures).

### Possible Causes (in order of likelihood)

#### A. Texture Assets Not Found (white squares)

`sc2_hud_image_index()` in `hud.c` maps logical texture names to physical paths:
```c
{ "UI/ResourceIcon0", "Assets/Textures/icon-mineral.dds" },
{ "UI/ResourceIcon1", "Assets/Textures/icon-gas.dds" },
{ "UI/ResourceIconSupply", "Assets/Textures/icon-supply.dds" },
```

These paths may not exist in the MPQ archives. The actual SC2 textures might use different naming or be in texture atlases. Check with `mpqtool`:
```bash
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Assets" ls Assets/Textures/ | grep -i "mineral\|gas\|supply"
```

#### B. Client-Side Layout Solver (`SCR_LayoutRect`)

The client's `SCR_LayoutRect` in `client/cl_layout.c` resolves frame rectangles from `uiFrame_t` points. If anchor chains don't resolve correctly (e.g., intermediate frames have wrong wire numbers or parent references), frames may be positioned off-screen.

Check: Does `SCR_LayoutRect` correctly handle the `parent=0` (UI_PARENT/scene root) case for all anchor chains?

#### C. Frame Visibility Flags

`SC2_FlattenFrame` sets `SC2_UIFLAG_HIDDEN` when `<Visible val="false"/>` is present. Check if any frames in the ResourcePanel → CashPanel anchor chain are marked hidden.

The ResourcePanel frame type `SC2_FRAMETYPE_RESOURCE_PANEL` (enum value 14) is in `SC2_MapFrameType`:
```c
case SC2_FRAMETYPE_RESOURCE_PANEL: return FT_FRAME;
```

#### D. Layer Separation

`hud_resource.c` writes to `LAYER_CONSOLE`. Other panels write to:
- `LAYER_BACKGROUND` — ConsolePanel
- `LAYER_COMMANDBAR` — CommandPanel
- `LAYER_INFOPANEL` — InfoPanel

Each layer is rendered independently. If frames overlap across layers, the drawing order depends on layer index. Check `client/cl_scrn.c` — `SCR_LayoutDrawOverlay` iterates layers in order and draws each.

#### E. Label Content

ResourcePanel children are `CountdownLabel` and `Image` frames. The `CountdownLabel` type maps to `FT_TEXT`:
```c
case SC2_FRAMETYPE_COUNTDOWN_LABEL: return FT_TEXT;
```

Labels need `.text` or `.stat` to render text. The `.stat` field is set by `resource_find()`, but `.text` might need to be populated with actual player state values at render time. Check if the client reads `.stat` to display dynamic values.

#### F. `SC2_HUD_BuildFrameForWrite` — `stat` Not Wired

`SC2_HUD_BuildFrameForWrite` in `hud.c` sets `out->stat = frame->stat`. The client needs to read this stat index and display the corresponding player state value. Check `SCR_LayoutDrawText` or equivalent to see if it handles `.stat`.

## Key Files

| File | Purpose |
|------|---------|
| `games/starcraft-2/common/stb_sc2layout.h` | Single-header SC2 layout parser + frame builder |
| `games/starcraft-2/game/hud/hud.c` | `sc2BaseFrame_t` → `uiFrame_t` bridge, frame numbering |
| `games/starcraft-2/game/hud/hud_resource.c` | Resource panel writer (binds stats, writes full tree) |
| `games/starcraft-2/game/hud/hud_console.c` | Console panel writer (writes ConsolePanel subtree) |
| `games/starcraft-2/game/hud/hud_command.c` | Command panel writer |
| `games/starcraft-2/game/hud/hud_infopanel.c` | Info panel writer |
| `client/cl_layout.c` | `SCR_ClearLayoutLayer`, `SCR_LayoutRect` — client layout solver |
| `client/cl_scrn.c` | `SCR_LayoutDrawOverlay`, `SCR_DrawLayout` — rendering dispatch |
| `client/cl_parse.c` | `CL_ParseLayout` — receives `svc_layout` from server |

## SC2 Layout Files (extracted via mpqtool)

```bash
# Extract layouts:
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" cat "UI/Layout/UI/GameUI.SC2Layout"
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" cat "UI/Layout/UI/ResourcePanel.SC2Layout"
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" cat "UI/Layout/UI/CashPanel.SC2Layout"
```

Key structure:
```
GameUI.SC2Layout
├── WorldPanel
├── OverlayImage
├── FadeImage
├── TopLetterboxImage
├── BottomLetterboxImage
└── UIContainer
    ├── FullscreenLowerContainer
    │   ├── PausePanel
    │   └── ConversationPanel
    ├── ConsolePanel
    └── ConsoleUIContainer
        ├── MinimapPanel
        ├── CommandPanel
        ├── InfoPanel
        ├── ControlGroupPanel
        ├── IdleButton, AIButton, PylonButton
        └── FullscreenUpperContainer
            ├── CharacterSheetButton (top=parent.min+5, right=parent.max-15, w=36, h=36)
            ├── AllianceButton (top=CharacterSheetButton.top, right=CharacterSheetButton.min-15)
            ├── TeamResourceButton (top=AllianceButton.top, right=AllianceButton.min-15)
            ├── CashPanel (top=parent.min, right=TeamResourceButton.min, w=280, h=60)
            ├── ResourcePanel (top=parent.min, right=CashPanel.min-15, w=800, h=42)
            │   ├── SupplyLabel (top=parent.min+5, right=parent.max, w=120, h=32)
            │   ├── SupplyIcon (top=parent.min+5, right=SupplyLabel.min-5, 32×32)
            │   ├── ResourceLabel3 → ResourceIcon3 → ... (chain of sibling refs)
            │   └── PlayerImage
            └── TalkerPanel (top=ResourcePanel.max+10, right=parent.max-10)
```

ResourcePanel anchor chain:
- Right edge → CashPanel.Min (left edge) offset −15px
- CashPanel right → TeamResourceButton.Min offset 0
- TeamResourceButton right → AllianceButton.Min offset −15
- AllianceButton right → CharacterSheetButton.Min offset −15
- CharacterSheetButton right → parent.Max (FullscreenUpperContainer right) offset −15

## How to Run and Debug

```bash
# Build and run:
make run-sc2 ARGS="+com_frame_limit 100"

# Run tests:
make test

# Force clean rebuild:
make -B run-sc2 ARGS="+com_frame_limit 100"

# Extract layout files:
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Data" cat "UI/Layout/UI/GameUI.SC2Layout"

# Check what textures exist:
build/bin/mpqtool -mpq "data/StarCraft2/Mods/Core.SC2Mod/Base.SC2Assets" ls Assets/Textures/ | grep -i "icon\|mineral\|gas\|supply"
```

## Next Steps

1. **Fix the build** — there may be a compilation issue from in-progress work. Verify `make run-sc2` compiles.

2. **Diagnose why the resource bar isn't visible** — add targeted `fprintf(stderr, ...)` logs to trace the issue:
   - Check if ResourcePanel frames are actually written to the wire (add count in `SC2_HUD_WriteFrame`)
   - Check if the client receives the frames (`CL_ParseLayout` logs)
   - Check if `SCR_LayoutRect` resolves anchors correctly for ResourcePanel's chain
   - Check if texture resources load successfully (the "white squares" suggest missing textures)

3. **Fix texture paths** — the `sc2_hud_image_index` mappings may point to non-existent DDS files. Find the correct SC2 texture paths for resource icons.

4. **Verify client-side rendering** — ensure `SCR_LayoutDrawTexture` and `SCR_LayoutDrawText` handle the ResourcePanel's `FT_FRAME`, `FT_TEXT`, and `FT_TEXTURE` children.

## Git State

- Branch: `feat/sc2-hud-layout-pipeline`
- Last pushed: `4dc31a2` — "fix(sc2-hud): refactor index systems and fix cross-panel anchors"
- Working tree may have uncommitted changes from in-progress debugging (check `git status`).

## AGENTS.md Rules

- **Never guess at a bug fix.** Add targeted logs, run with `+com_frame_limit N`, read output, confirm root cause, then write the fix.
- **No hacks.** Mark shortcuts with `/* HACK: */` or `/* TODO: */` and explain why.
- **Run `make test` before committing.** 421 tests must pass.
- **Verify in both ROC and TFT** (use `-tft` flag).
- **Use `git blame` when investigating history.**
