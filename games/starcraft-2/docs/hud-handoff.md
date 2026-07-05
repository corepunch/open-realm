# SC2 HUD Pipeline — Handoff Notes

Branch: `feat/sc2-hud-layout-pipeline`  
Issue: #82  
Last commit: `a8f4e25 fix(sc2-hud): template resolution two-pass + layer separation + button FT_FRAME`

---

## What is done

### Parser + template resolution (`stb_sc2layout.h`)
- XML → `sc2Frame_t[]` templates, then flattened to `sc2BaseFrame_t[]` via `SC2_LayoutFlatten("GameUI")`
- **Two-pass template resolution**: per-file pass (inside `SC2_ParseDescNode`) resolves same-file and ready cross-file templates immediately, clearing `template_path[0]` on success. Global post-pass (inside `SC2_LayoutBuildGameUI`) handles any remaining forward references. The clearing on success is critical — without it the global pass re-resolves and doubles every child list.
- **`core_files[]` ordering** is leaf-to-root (`CommandButton` before `CommandPanel`, `PortraitPanel` before `ConsolePanel`, `GameUI` last).
- Shorthand `<Anchor relative="$parent"/>` (no `side`/`pos`) expands to four anchors filling the parent.
- `SC2_FRAMETYPE_BUTTON` and `SC2_FRAMETYPE_COMMAND_BUTTON` map to `FT_FRAME` (not `FT_BUTTON`) — SC2 buttons are containers; `FT_BUTTON` on the client crashes `SCR_LayoutGlueTextButton`.
- `SC2_FRAMETYPE_IMAGE` maps to `FT_TEXTURE`; `SC2_FRAMETYPE_MODEL` maps to `FT_SPRITE`.
- `SC2_FRAMETYPE_COUNTDOWN_LABEL` (used for resource labels in ResourcePanel) maps to `FT_TEXT`. ✓

### HUD bridge (`hud.c`, `hud_resource.c`, `hud_console.c`, `hud_command.c`, `hud_infopanel.c`)
- `SC2_HUD_InitLayoutHost()` wires `uiimport` (file I/O, `ImageIndex`, `FontIndex`).
- `SC2_HUD_EnsureLayout()` loads the layout once; falls back to programmatic frames if SC2 data is missing.
- `SC2_HUD_BuildFrameForWrite()` converts `sc2BaseFrame_t` → `uiFrame_t` (anchors, color, tex, stat/text, label buffer).
- `SC2_HUD_WriteAncestors` + `SC2_HUD_WriteFrameWithChildren` for correct wire ordering (parents before children).
- **Layer separation**: LAYER_BACKGROUND carries only ConsolePanel subtree + bare ConsoleUIContainer + MinimapPanel. CommandPanel and InfoPanel are on LAYER_COMMANDBAR / LAYER_INFOPANEL.
- **Texture catalog**: `sc2_hud_load_assets_txt("GameData/Assets.txt")` populates a runtime `assets_catalog[]` for Liberty.SC2Mod entries. Core.SC2Mod entries that Liberty doesn't repeat are hardcoded in `paths[]`.
- `sv_debug_layout 1` server cvar prints per-layer wire diagnostics.

### Tests (`test_sc2_consoleui.c`)
All 565 SC2 tests pass. Groups cover: parser bug regressions, anchor resolution, flatten/frame population, screen-rect pipeline.

---

## What is NOT done — two visual regressions

After fixing template doubling the game produces 294 frames (down from 438 duplicated). Two things broke:

### Regression 1 — Resource labels show "text N" instead of stat values

**Symptom**: The resource bar displays `text 28`, `text 26`, etc. instead of `0` mineral/gas/supply counts.

**Root cause**: `SCR_GetStringValue` in `cl_layout.c` falls back to `"text %d"` (wire number) when `frame->stat == 0 && frame->text == NULL`. This means the stat binding in `resource_find()` (`hud_resource.c`) is not reaching the label frames.

**Why**: `resource_find()` calls `SC2_LayoutFindChildFrame(root, "ResourceLabel0")`. This searches for a **direct child** of ResourcePanel in the flat array (`parent_index == root->number`). With the fix in place, need to verify that ResourceLabel0 etc. are actually direct children of the ResourcePanel flat frame after flattening. The SC2Layout structure is:

```
ResourcePanel.SC2Layout:
  <Frame type="ResourcePanel" name="ResourcePanelTemplate">
    <Frame type="CountdownLabel" name="ResourceLabel0" .../>
    <Frame type="CountdownLabel" name="ResourceLabel1" .../>
    ...
    <Frame type="Label"          name="SupplyLabel"    .../>
```

After `SC2_LayoutFlatten("GameUI")`, ResourceLabel0 should be at `parent_index = ResourcePanel->number`. If it isn't (e.g. parent is the template definition rather than the instance), `SC2_LayoutFindChildFrame` returns NULL and stat is never set.

**How to diagnose**: add a temporary `fprintf` in `resource_find()` after the child lookup to check if `label == NULL`.

**Likely fix candidates**:
- Verify `SC2_FlattenFrame` correctly sets `parent_index` for template-expanded children (the children are added via `SC2_ResolveTemplate` → `frame->children[i]`, then `SC2_FlattenFrame` recurses with `parent_index = index` of the instance). This chain looks correct in the code — but run with logging to confirm.
- If children are one level deeper (grandchildren via a sub-template like `ResourcePanelLabelTemplate`), `SC2_LayoutFindChildFrame` (direct-child only) won't reach them. In that case change `resource_find()` to use `SC2_LayoutFindFrameByName("ResourceLabel0")` instead, which scans the full flat array.

### Regression 2 — Large portrait rectangles appear in the center of the screen

**Symptom**: Two large black rectangles with SC2 rounded-corner borders visible in the center of the game viewport.

**Root cause**: With template resolution now working correctly, `ConsolePanelTemplate` has its full child list including a `PortraitPanel` child (144×219). `PortraitPanel` expands via its own template (`PortraitPanelTemplate`) to a `Background` child (FT_TEXTURE, `UI/BlankPortraitBackground` — the blank portrait texture with SC2 rounded borders) and a `Portrait` child (FT_SPRITE). `hud_console.c` calls `SC2_HUD_WriteFrameWithChildren(console)` which recursively writes ALL of ConsolePanel's children, so Background and Portrait get written and rendered.

In the SC2 game, PortraitPanel is hidden by default and only shown when a unit is selected. It needs to be treated the same way here.

**Fix**: The cleanest approach is to set `SC2_UIFLAG_HIDDEN` on the PortraitPanel flat frame right after `SC2_HUD_EnsureLayout()` loads the layout. It should only be cleared by `hud_infopanel.c` when a unit is selected. Add this to `SC2_HUD_InitLayoutHost` or to the end of `SC2_LayoutBuildGameUI`:

```c
/* Hide PortraitPanel by default — shown only on unit select */
sc2BaseFrame_t *portrait = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_PORTRAIT_PANEL);
if (portrait) portrait->ui_flags |= SC2_UIFLAG_HIDDEN;
```

Since `SC2_HUD_WriteFrameWithChildren` already skips `SC2_UIFLAG_HIDDEN` frames (and their children), this single line stops the background rectangle from rendering.

---

## Key file locations

| File | Role |
|------|------|
| `games/starcraft-2/common/stb_sc2layout.h` | Single-header parser. All template resolution, flattening, anchor math lives here. |
| `games/starcraft-2/game/hud/hud.c` | Bridge: `sc2BaseFrame_t → uiFrame_t`, shared layout load, fallback frames, `sc2_hud_image_index`. |
| `games/starcraft-2/game/hud/hud_resource.c` | Writes LAYER_CONSOLE: ResourcePanel + stat bindings. Anchor chain for CashPanel right-edge. |
| `games/starcraft-2/game/hud/hud_console.c` | Writes LAYER_BACKGROUND: ConsolePanel model backdrop + MinimapPanel. |
| `games/starcraft-2/game/hud/hud_command.c` | Writes LAYER_COMMANDBAR: CommandPanel ability grid. |
| `games/starcraft-2/game/hud/hud_infopanel.c` | Writes LAYER_INFOPANEL: selected unit stats/portrait. |
| `games/starcraft-2/tests/test_sc2_consoleui.c` | All adapter tests. |
| `games/starcraft-2/docs/hud-layout-pipeline.md` | Design doc (up to date). |
| `client/cl_layout.c` | `SCR_GetStringValue`: `stat > 0` → player stat, `text != NULL` → literal, else `"text %d"`. |

## How to run

```sh
# Tests
make test-sc2

# Headless run with layout diagnostics
build/bin/opensc2 -data data/StarCraft2 +dedicated 1 +map TRaynor01 \
  +set sv_debug_layout 1 +com_frame_limit 3

# Full game
make run-sc2
# (touch games/starcraft-2/game/hud/hud.c first if stb_sc2layout.h changed)
```
