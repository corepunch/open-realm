# SC2 ConsoleUI via WC3 Renderer Reuse — Implementation Plan

## Context

The SC2 `.SC2Layout` XML parser (`sc2_layout.c`, 924 lines) exists on `feature/ui-refactoring`
(commit `22ebb5e0`). It parses XML into `sc2Frame_t` trees, resolves templates/constants, and
flattens into `uiBaseFrame_t[]`. The client already has `CL_UIBaseLayoutRect()` (`cl_layout.c`)
that solves layout for `uiBaseFrame_t` directly — no need to route through WC3's `FRAMEDEF`.

The parser has 4 known bugs and the flatten/adapter step is incomplete (anchors stubbed,
textures unresoled, event handlers unwired). The `ui_main.c` entry point is all stubs.

**Goal**: Fix the parser, complete the adapter, and wire up enough SC2 ConsoleUI rendering
to see the in-game HUD frames positioned and drawn.

---

## Phase 1: Fix Parser Bugs (4 fixes in `sc2_layout.c`)

### Bug 1 — `##` constant resolution (line ~238)
`SC2_LayoutResolveConstant()` only strips one `#`. SC2 uses `##Name` prefix.
**Fix**: `while (*name == '#') name++;` instead of single `if` + `name++`.

### Bug 2 — Template inheritance ordering (same-file templates)
When `TestButtonTemplate` references `TestBaseTemplate` in the same file, the base
template may not have its properties populated yet during sequential parsing.
**Fix**: Add a second resolution pass after all frames in a file are parsed. Or defer
template property copying to flatten time when all templates are guaranteed complete.

### Bug 3 — Constant resolution in anchor offsets
`SC2_ParseAnchor` reads offset via `atoi()` directly, never calling
`SC2_LayoutResolveConstant()` on the raw string first.
**Fix**: Read offset as string, resolve `##constant`, then `atoi()`. Same for
`<Width val="..."/>` and `<Height val="..."/>` children.

### Bug 4 — Cross-file template inheritance
`IncludedPanel` referencing `template="TestIncluded/IncludedFrame"` from an included
file — the template may not be found or may lack properties at resolution time.
**Fix**: Verify `SC2_ResolveTemplatePath` basename lookup works across files; ensure
included templates are fully parsed before consumers.

**Files**: `games/starcraft-2/ui/sc2_layout.c`
**Verify**: `build/bin/test_sc2` — aim for 236/236 assertions passing.

---

## Phase 2: Complete the Anchor Adapter (`SC2_ResolveAnchors`)

Map SC2's 4-side anchor model to `uiBaseFrame_t.points.x/y[3]`:

```
SC2 side   axis   pos    →  frame point
─────────────────────────────────────────
Top        y      Min    →  points.y[FPP_MIN]
Bottom     y      Max    →  points.y[FPP_MAX]
Left       x      Min    →  points.x[FPP_MIN]
Right      x      Max    →  points.x[FPP_MAX]
```

For each active anchor:
1. Map `sc2Anchor_t.side` → axis (x or y)
2. Map `sc2Anchor_t.pos` → `FPP_MIN`, `FPP_MID`, or `FPP_MAX`
3. Resolve `sc2Anchor_t.relative` (`"$parent"` → `parent_index`, `"$root"` → 0,
   named frame → lookup by name in the flat array)
4. Convert `sc2Anchor_t.offset` (int16 pixels) to FLOAT normalized offset

The `CL_UIBaseSolveAxisPosition()` in `cl_layout.c` already handles:
- Single anchor (min or max only) → position + size
- Dual anchors (min+max) → stretch to fill
- Mid anchor → center on point

**Files**: `games/starcraft-2/ui/sc2_layout.c` (`SC2_ResolveAnchors` function)

---

## Phase 3: Complete Flatten Step

In `SC2_FlattenFrame()`, populate remaining `uiBaseFrame_t` fields:

### Textures
- `frame->textures[0].resource` → `dst->image` (store raw reference; renderer resolves)
- `frame->textures[0].tiled` → `dst->backdrop.tile`
- Texture type mapping: `"Normal"` → `dst->image`, `"Border"` → `dst->backdrop.edge`,
  `"HorizontalBorder"` → backdrop edge with tile flag

### Text
- SC2 `Label` frames with inline text → `dst->text`
- SC2 frames with `<Text val="..."/>` child → `dst->text`

### Backdrop
- First tiled texture → `dst->backdrop.bg`
- Border texture → `dst->backdrop.edge`
- Color → `dst->color`

### Event handlers
- Assign `dst->on_event` for buttons (SC2 command buttons need click handling)
- Assign `dst->on_draw` for SC2-specific panel types that need custom rendering

**Files**: `games/starcraft-2/ui/sc2_layout.c` (`SC2_FlattenFrame`, `SC2_ResolveAnchors`)

---

## Phase 4: Wire `ui_main.c` Callbacks

Fill in the stub functions in `games/starcraft-2/ui/ui_main.c`:

### `SC2_UI_MouseEvent`
1. Convert pixel coords to FDF space (use `SCR_MouseToFdf` equivalent)
2. Hit-test against `uiBaseFrame_t[]` (iterate back-to-front, point-in-rect)
3. Set `UIFLAG_HOVERED` / `UIFLAG_PRESSED` on hit frame
4. Dispatch to frame's `on_event` callback

### `SC2_UI_HitTestLayout`
Iterate frames, return true if any visible non-hidden frame contains the point.

### `SC2_UI_KeyEvent`
Forward to focused frame (editbox) if any.

### `SC2_UI_Refresh`
Increment time, update animations if any.

### `SC2_UI_UpdateUnitUI`
Store unit data for HUD panels to read during draw.

**Files**: `games/starcraft-2/ui/ui_main.c`

---

## Phase 5: SC2-Specific Frame Renderers

For the ConsoleUI HUD, these SC2 frame types need custom `on_draw` callbacks:

### `CommandButton` (SC2_FRAMETYPE_COMMAND_BUTTON)
Draw ability icon from unit data, cooldown overlay, hotkey label.
Similar to WC3's `FT_COMMANDBUTTON` but uses SC2's XML-defined grid.

### `ResourcePanel` (SC2_FRAMETYPE_RESOURCE_PANEL)
Draw mineral/gas/supply counts. Reads from game state.

### `PortraitPanel` (SC2_FRAMETYPE_PORTRAIT_PANEL)
Render 3D model portrait of selected unit. Uses renderer's model API.

### `Minimap` (SC2_FRAMETYPE_MINIMAP)
Render minimap terrain + units. Uses renderer's minimap API.

### `InfoPanel` (SC2_FRAMETYPE_INFO_PANEL)
Draw selected unit's name, health bar, stats.

### Generic frames
All SC2 types mapped to `FT_FRAME` just render children — no custom draw needed.
The layout solver positions them; the client renders their children recursively.

**Files**: `games/starcraft-2/ui/ui_render.c` (new file), `games/starcraft-2/ui/ui_main.c`

---

## Phase 6: Integration and Testing

1. Build: `make opensc2` should link with `libui-sc2`
2. Run: `build/bin/opensc2 -data "..." +map "Maps/Test/MarSara.SC2Map"`
3. Verify: SC2 ConsoleUI frames appear positioned on screen
4. Test: `build/bin/test_sc2` passes all 14 layout test cases

---

## Key Files

| File | Action |
|------|--------|
| `games/starcraft-2/ui/sc2_layout.c` | Fix 4 bugs, complete `SC2_ResolveAnchors`, complete `SC2_FlattenFrame` |
| `games/starcraft-2/ui/sc2_layout.h` | No changes needed |
| `games/starcraft-2/ui/ui_main.c` | Wire event callbacks, add hit test, add mouse/key handlers |
| `games/starcraft-2/ui/ui_render.c` | New — SC2-specific frame draw callbacks |
| `client/cl_layout.c` | Already works — no changes needed |
| `client/ui.h` | Already has `uiBaseFrame_t` — no changes needed |
| `games/starcraft-2/tests/test_sc2_layout.c` | May need assertion updates after bug fixes |

## What We Get From WC3 (Free)

- Layout constraint solver (`CL_UIBaseLayoutRect` in `cl_layout.c`)
- Scene rect computation (`SCR_GetUISceneRect`)
- Mouse-to-coordinate conversion (`SCR_MouseToFdf`)
- Frame hit testing infrastructure
- All generic frame type rendering (containers, images, text, buttons)
- The `uiImport_t`/`uiExport_t` contract

## What We Build New

- SC2 anchor model → `uiBaseFrame_t.points` adapter
- SC2 `.SC2Layout` XML → `sc2Frame_t` parser (already done, needs bug fixes)
- SC2-specific frame draw callbacks (CommandButton, ResourcePanel, PortraitPanel, Minimap, InfoPanel)
- SC2 event dispatch (mouse, keyboard)
