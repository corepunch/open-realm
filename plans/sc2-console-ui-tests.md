# SC2 ConsoleUI Tests — Plan

## Context

The SC2 layout parser (`sc2_layout.c`) has 14 existing tests on `feature/ui-refactoring`
(commit `22ebb5e0`). These test parsing, templates, constants, includes, and basic
flattening. The adapter work (anchor resolution, texture/image mapping, flatten completion,
event wiring) has zero test coverage. The 4 known parser bugs also have no regression tests.

**Goal**: Add tests for the adapter layer, bug fixes, and frame rendering pipeline so
the ConsoleUI can be built with confidence.

---

## Test File

**New file**: `games/starcraft-2/tests/test_sc2_consoleui.c`

Follows the existing pattern from `test_sc2_layout.c`:
- Includes `"common.h"`, `"games/starcraft-2/ui/sc2_layout.h"`, `"test_framework.h"`
- Defines `uiImport_t uiimport` global
- Uses `setup_sc2_layout_tests()` (idempotent, guarded by static bool)
- Each test: `SC2_LayoutInit()` → parse fixture → assert → `SC2_LayoutShutdown()`

**Runner update**: `games/starcraft-2/tests/test_sc2_main.c`
- Add `void run_sc2_consoleui_tests(void);` declaration
- Call `run_sc2_consoleui_tests()` in `main()`

**Makefile update**: Add `$(SC2_TEST_DIR)/test_sc2_consoleui.c` to the `test-sc2` source list.

---

## New Test Fixture

**File**: `games/starcraft-2/tests/resources-src/UI/Layout/TestAdapter.SC2Layout`

A purpose-built layout that exercises every adapter code path:

```xml
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<Desc>
    <!-- Constants for offset/size testing -->
    <Constant name="HUDMargin" val="8"/>
    <Constant name="PanelWidth" val="200"/>

    <!-- Root container with all 4 anchor sides -->
    <Frame type="GameUI" name="ConsoleUI">
        <Anchor relative="$parent"/>

        <!-- Bottom-left panel: tests Left+Bottom anchors -->
        <Frame type="Frame" name="ResourcePanel">
            <Width val="200"/>
            <Height val="40"/>
            <Anchor side="Left" relative="$parent" pos="Min" offset="#HUDMargin"/>
            <Anchor side="Bottom" relative="$parent" pos="Max" offset="-40"/>
            <Anchor side="Top" relative="$parent" pos="Max" offset="-80"/>
            <Visible val="true"/>
            <Color val="255,255,255,200"/>

            <Frame type="Image" name="MineralIcon">
                <Width val="32"/>
                <Height val="32"/>
                <Anchor side="Left" relative="$parent" pos="Min" offset="4"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="4"/>
                <Texture val="@@UI/MineralIcon" layer="0"/>
            </Frame>

            <Frame type="Label" name="MineralCount">
                <Width val="80"/>
                <Height val="20"/>
                <Anchor side="Left" relative="$parent/MineralIcon" pos="Max" offset="4"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="8"/>
                <Visible val="true"/>
            </Frame>
        </Frame>

        <!-- Top-right panel: tests Right+Top anchors with stretch -->
        <Frame type="Frame" name="InfoPanel">
            <Width val="250"/>
            <Height val="120"/>
            <Anchor side="Right" relative="$parent" pos="Max" offset="-8"/>
            <Anchor side="Top" relative="$parent" pos="Min" offset="8"/>
            <Visible val="true"/>

            <Frame type="Label" name="UnitName">
                <Width val="200"/>
                <Height val="20"/>
                <Anchor side="Left" relative="$parent" pos="Min" offset="8"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="8"/>
            </Frame>

            <Frame type="Image" name="Portrait">
                <Width val="96"/>
                <Height val="96"/>
                <Anchor side="Right" relative="$parent" pos="Max" offset="-8"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="32"/>
                <Texture val="@@UI/Portrait" layer="0"/>
            </Frame>
        </Frame>

        <!-- Command grid: tests Right+Bottom with child grid layout -->
        <Frame type="Frame" name="CommandArea">
            <Width val="308"/>
            <Height val="154"/>
            <Anchor side="Right" relative="$parent" pos="Max" offset="0"/>
            <Anchor side="Bottom" relative="$parent" pos="Max" offset="0"/>

            <Frame type="CommandButton" name="Cmd01">
                <Width val="76"/>
                <Height val="76"/>
                <Anchor side="Left" relative="$parent" pos="Min" offset="0"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
                <Texture val="@@UI/ButtonNormal" layer="0"/>
            </Frame>

            <Frame type="CommandButton" name="Cmd02">
                <Width val="76"/>
                <Height val="76"/>
                <Anchor side="Left" relative="$parent/Cmd01" pos="Max" offset="0"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
            </Frame>

            <Frame type="CommandButton" name="Cmd03">
                <Width val="76"/>
                <Height val="76"/>
                <Anchor side="Left" relative="$parent/Cmd02" pos="Max" offset="0"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
            </Frame>
        </Frame>

        <!-- Center label: tests Mid anchor -->
        <Frame type="Label" name="CenterAlert">
            <Width val="300"/>
            <Height val="40"/>
            <Anchor side="Left" relative="$parent" pos="Mid" offset="0"/>
            <Anchor side="Top" relative="$parent" pos="Mid" offset="0"/>
            <Visible val="false"/>
        </Frame>

        <!-- Hidden panel: tests visibility in hierarchy -->
        <Frame type="Frame" name="HiddenPanel">
            <Width val="100"/>
            <Height val="100"/>
            <Anchor side="Left" relative="$parent" pos="Min" offset="0"/>
            <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
            <Visible val="false"/>

            <Frame type="Label" name="HiddenChild">
                <Width val="50"/>
                <Height val="20"/>
                <Anchor side="Left" relative="$parent" pos="Min" offset="0"/>
                <Anchor side="Top" relative="$parent" pos="Min" offset="0"/>
            </Frame>
        </Frame>
    </Frame>
</Desc>
```

This fixture provides:
- All 4 anchor sides (Top, Bottom, Left, Right) with Min/Mid/Max positions
- Constant references in offsets (`#HUDMargin`)
- Cross-frame relative anchors (`$parent/Cmd01`)
- Multiple child depths (root → panel → icon/label)
- Texture references for image resolution testing
- Visibility flags (visible + hidden + hidden hierarchy)
- CommandButton frames for SC2-specific type testing
- Stretch layout (two anchors on same axis: ResourcePanel has Left+Bottom but also Top+Bottom for vertical stretch)
- Mid anchor (CenterAlert)

---

## Test Cases (16 tests)

### Group 1: Parser Bug Fixes (4 tests)

These verify the 4 known bugs are fixed. They serve as regression tests.

#### `test_adapter_constant_hash_stripping`
Parse `TestAdapter.SC2Layout`. Verify `SC2_LayoutResolveConstant("##HUDMargin")`
returns `"8"` and `SC2_LayoutResolveConstant("##PanelWidth")` returns `"200"`.
Also test double-hash specifically: `"##HUDMargin"` must resolve (Bug 1 fix).

#### `test_adapter_constant_offset_resolves`
Parse `TestAdapter.SC2Layout`. Find `ResourcePanel` frame. Verify
`anchors[0].offset` == 8 (resolved from `#HUDMargin`). This tests Bug 3 fix:
constants in anchor offsets must be resolved before atoi.

#### `test_adapter_template_inheritance_ordering`
Parse `TestGameUI.SC2Layout`. Find `ActionButton01`. Verify it inherited
width=300, height=75 from `TestButtonTemplate` and has 3 anchors (2 from base
+ 1 override). This tests Bug 2 fix: same-file template ordering.

#### `test_adapter_cross_file_template_inheritance`
Parse `TestGameUI.SC2Layout`. Find `IncludedPanel`. Verify it inherited
width=100, height=50 from `IncludedFrame` in `TestIncluded.SC2Layout`.
This tests Bug 4 fix: cross-file template resolution.

### Group 2: Anchor Resolution (5 tests)

These test `SC2_ResolveAnchors` — the core adapter mapping SC2 anchors to
`uiBaseFrame_t.points`.

#### `test_adapter_single_anchor_left_min`
Parse `TestAdapter.SC2Layout`. Flatten. Find `MineralIcon` frame.
Verify `points.x[FPP_MIN].used == true`, `points.x[FPP_MIN].targetPos == FPP_MIN`,
`points.x[FPP_MIN].relative_index` resolves to `ResourcePanel`'s index.

#### `test_adapter_single_anchor_top_min`
Find `MineralIcon`. Verify `points.y[FPP_MIN].used == true`,
`points.y[FPP_MIN].targetPos == FPP_MIN`.

#### `test_adapter_dual_anchor_stretch`
Find `ResourcePanel`. It has `Left+Min` and `Right+Max` on x-axis? No — it has
`Left+Min` only. Let me re-check... Actually the fixture has ResourcePanel with
Left+Bottom+Top anchors. Top+Bottom on y-axis = stretch. Verify
`points.y[FPP_MIN].used == true` and `points.y[FPP_MAX].used == true` — dual
anchor stretch on y-axis.

#### `test_adapter_mid_anchor`
Find `CenterAlert`. Verify `points.x[FPP_MID].used == true` and
`points.y[FPP_MID].used == true`. The mid anchor centers the frame.

#### `test_adapter_cross_frame_relative`
Find `Cmd02`. Its Left anchor references `$parent/Cmd01`. Verify
`points.x[FPP_MIN].relative_index` resolves to Cmd01's frame index (not parent).

### Group 3: Flatten / Frame Population (4 tests)

These test `SC2_FlattenFrame` — correct population of `uiBaseFrame_t` fields.

#### `test_adapter_flatten_frame_count`
Parse `TestAdapter.SC2Layout`. Flatten. Count should be exactly 15 frames
(ConsoleUI + ResourcePanel + MineralIcon + MineralCount + InfoPanel + UnitName
+ Portrait + CommandArea + Cmd01 + Cmd02 + Cmd03 + CenterAlert + HiddenPanel
+ HiddenChild = 14, plus root = 15). Verify count matches.

#### `test_adapter_flatten_types_mapped`
Verify `SC2_MapFrameType` mapping for each type in the fixture:
- `ConsoleUI` → `FT_FRAME`
- `Cmd01` → `FT_BUTTON`
- `MineralIcon` → `FT_SPRITE`
- `MineralCount` → `FT_TEXT`
- `Portrait` → `FT_SPRITE`

#### `test_adapter_flatten_hidden_flags`
Find `HiddenPanel`: verify `hidden == true`, `ui_flags & UIFLAG_HIDDEN`.
Find `HiddenChild`: verify it's a child of HiddenPanel in the flat array.
Find `CenterAlert`: verify `hidden == true` (Visible val="false").
Find `ResourcePanel`: verify `hidden == false`.

#### `test_adapter_flatten_color_alpha`
Find `ResourcePanel`. Verify `color` == `{255, 255, 255, 200}` (from XML).
Find `ConsoleUI`. Verify `color` == `{255, 255, 255, 255}` (default white).

### Group 4: Screen Rect Computation (3 tests)

These test the full pipeline: parse → flatten → `CL_UIBaseLayoutRect` (client layout solver).
Since the test binary doesn't link the client, we test the flatten output has the
data the client needs, and add a minimal local layout solver for verification.

#### `test_adapter_screen_rect_root_covers_scene`
Parse + flatten. The root `ConsoleUI` frame has `Anchor relative="$parent"`.
After the client's layout solver runs, root should cover the full scene rect.
We verify by checking that root's `parent_index == (DWORD)-1` (indicating it
attaches to the scene root).

#### `test_adapter_screen_rect_child_relative_to_parent`
Find `MineralCount`. Its Left anchor references `$parent/MineralIcon`.
Verify `points.x[FPP_MIN].relative_index` is NOT the parent index — it's
MineralIcon's index. This confirms cross-frame references work.

#### `test_adapter_screen_rect_hidden_no_draw`
Find `HiddenPanel` in flat array. Verify `hidden == true`. The client skips
drawing hidden frames, so this confirms the flag is set correctly.

---

## Files Changed

| File | Change |
|------|--------|
| `games/starcraft-2/tests/test_sc2_consoleui.c` | **New** — 16 test cases |
| `games/starcraft-2/tests/test_sc2_main.c` | Add `run_sc2_consoleui_tests()` call |
| `games/starcraft-2/tests/resources-src/UI/Layout/TestAdapter.SC2Layout` | **New** — test fixture |
| `Makefile` | Add `test_sc2_consoleui.c` to `test-sc2` source list |

## Build & Verify

```bash
make test-sc2
# Should show: 185+16 = ~201 layout+adapter assertions, all passing
```

## Dependency

These tests depend on the adapter implementation being complete (Phases 1-3 of the
implementation plan). The bug fix tests (Group 1) can be written and run immediately
as regression tests — they should pass once the fixes are applied. The anchor/flatten/
screen rect tests (Groups 2-4) require the adapter code to be implemented first.
