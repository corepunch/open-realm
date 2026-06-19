# WoW Character Create — Model Rendering Fix

## Problem

The CharacterCreate background model doesn't render. Race swap updates the model path
but the screen stays blank. The Lua side is complete; the C plumbing has gaps.

## Root Causes Found

### 1. `hidden="true"` on the Model element is never cleared
`CharacterCreate.xml:192` declares `<Model name="CharacterCreate" ... hidden="true">`.
`SetGlueScreen("charcreate")` (Lua `GlueParent.lua:34-58`) iterates `GlueScreenInfo`,
calls `frame:Hide()` on every screen, then `newFrame:Show()` on the target.
Our C `UIWow_LuaSetGlueScreen` (ui_xml.c:681-705) does the same via `UIWow_XMLSetShown`.
However `UIWow_XMLSetShown` clears `EF_HIDDEN` on the frame itself — but `UIWow_XMLIsVisible`
walks the **parent chain**. The `CharacterCreate` Model has `parent="GlueParent"`, and
`GlueParent` is never explicitly shown after XML load. If GlueParent starts hidden or is
set hidden by the first `Hide()` loop, the entire subtree (including CharacterCreate) is
invisible to `UIWow_XMLIsVisible` even after `Show()`.

**Fix:** After loading GlueXML, ensure GlueParent is explicitly shown. Add
`UIWow_XMLSetShown(glueparent_idx, true)` at the end of `UIWow_XMLLoadGlueFromToc`, or
have `UIWow_LuaSetGlueScreen` only hide/show the glue screen frames, not GlueParent.

### 2. `AdvanceTime` works but is only called from `OnUpdateModel`
`CharacterCreate_UpdateModel()` (CharacterCreate.lua:293-296) calls `UpdateCustomizationScene()` (noop)
then `this:AdvanceTime()`. `AdvanceTime` increments `e->frame`. The draw code at ui_xml.c:1685-1702
passes `e->frame` to `DrawPortrait`. This is correct **if** the model is visible and the draw path runs.

### 3. Race swap: model path updates correctly
`CharacterRace_OnClick` → `SetSelectedRace` → `SetCharacterRace` → `SetBackgroundModel` →
`SetCharCustomizeBackground` → `UIWow_XmlSetFrameModel` updates `ELEM_FILE` and releases old model.
The draw path will reload the new model on next frame. This is correct **if** the model is visible.

### 4. Description panels work
`SetCharacterRace` updates faction/race/class text panels from DBC-backed global strings. No issue here.

## Whoa-master Reference

- `data/whoa-master/src/glue/CCharacterCreation.cpp` — `CreateComponent()` creates the character
  model and attaches it as a child of the background model via `model->AttachToParent()`
- `data/whoa-master/src/glue/CCharacterCreationScript.cpp` — 32 Lua-callable functions bridging
  Lua to C++ (SetCharCustomizeFrame, SetCharCustomizeBackground, ResetCharCustomize, etc.)
- `data/whoa-master/src/glue/CGlueMgr.cpp:996-1017` — initialization: registers script functions,
  calls `CCharacterCreation::Initialize()`, loads GlueXML, signals FRAMES_LOADED
- `data/whoa-master/src/object/client/CGPlayer_C.cpp:114-138` — `Player_C_GetModelName()`
  resolves race+sex to model path via ChrRaces → CreatureDisplayInfo → CreatureModelData DBCs

## Lua Reference (already correct, no changes needed)

- `games/world-of-warcraft/tests/resources-src/Interface/GlueXML/CharacterCreate.lua`
  - `CharacterCreate_OnLoad():60` — `SetCharCustomizeFrame("CharacterCreate")`
  - `CharacterCreate_OnShow():73-89` — `ResetCharCustomize()`, enumerate races/classes
  - `CharacterRace_OnClick():314-324` — `SetSelectedRace(id)` + `SetCharacterRace(id)`
  - `SetCharacterRace():164-250` — updates panels, calls `SetBackgroundModel(CharacterCreate, fileString)`
  - `CharacterCreate_UpdateModel():293-296` — `UpdateCustomizationScene()` + `this:AdvanceTime()`
- `games/world-of-warcraft/tests/resources-src/Interface/GlueXML/GlueParent.lua`
  - `SetGlueScreen():34-58` — hide all, show target
  - `SetBackgroundModel():177-205` — builds model path, sets fog

## C Reference (our implementation)

| Function | File:Line | Status |
|---|---|---|
| `UIWow_LuaSetCharCustomizeFrame` | ui_lua.c:507-514 | ✅ Implemented |
| `UIWow_LuaSetCharCustomizeBackground` | ui_lua.c:516-523 | ✅ Implemented |
| `UIWow_LuaUpdateCustomizationScene` | ui_lua.c:525-528 | ⚠️ No-op (intentional) |
| `UIWow_LuaFrameAdvanceTime` | ui_xml.c:608-614 | ✅ Implemented (increments frame) |
| `UIWow_LuaFrameSetFogColor/Near/Far` | ui_xml.c:616-652 | ✅ Implemented |
| `UIWow_XmlSetFrameModel` | ui_xml.c:2067-2075 | ✅ Implemented |
| Model draw path | ui_xml.c:1685-1702 | ✅ Implemented |
| `UIWow_LuaSetGlueScreen` | ui_xml.c:681-705 | ✅ Implemented |
| `UIWow_XMLIsVisible` | ui_xml.c:1593-1600 | ⚠️ Walks parent chain |

## Implementation Plan

### Step 1: Ensure GlueParent is visible after XML load
After `UIWow_XMLLoadGlueFromToc` loads all frames, find the GlueParent element and call
`UIWow_XMLSetShown(idx, true)` on it. This ensures the parent chain is visible.

**File:** `ui_xml.c` — add at end of `UIWow_XMLLoadGlueFromToc`

### Step 2: Verify GlueParent is not hidden by SetGlueScreen
`UIWow_LuaSetGlueScreen` iterates `GlueScreenInfo` and hides all entries. GlueParent is
NOT in `GlueScreenInfo`, so it should not be hidden. Verify this is the case.

### Step 3: Build and test
```
make run-ui-text UI_CMD=menu_character_create
```
Check that:
- Background model loads (load_texture for UI_NightElf/UI_Human.mdx)
- OnUpdateModel fires and AdvanceTime increments frame
- Race swap changes the background model
- Fog colors update per race

## Verification

1. `make` — clean build
2. `make run-ui-text UI_CMD=menu_character_create` — check stdout for model loading
3. If stdout renderer: check `draw_portrait` lines appear for the character model
