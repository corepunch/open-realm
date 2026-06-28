# WoW Character Create Screen

## Goal
Implement the WoW character creation screen with working race/class selection that updates the character model preview and background.

## What's Done

### Lua Bindings
- `SetCharCustomizeFrame` - stores frame index for model updates
- `SetCharCustomizeBackground` - updates model path via `UIWow_XmlSetFrameModel`
- `UpdateCustomizationScene` - stub (no-op)
- `AdvanceTime` - increments per-element animation frame counter
- `SetFogColor`, `SetFogNear`, `SetFogFar`, `ClearFog` - store fog params on element

### Model Element Support
- Added `ELEM_ON_UPDATE_MODEL` event handling in XML parsing and draw path
- Added `frame` counter (`DWORD`), `has_fog`, `fog_color`, `fog_near`, `fog_far` fields to `uiWowXmlElem_t`
- Added `id` field to Lua frame table for `this:GetID()` in OnClick handlers

### Portrait API
- Changed `DrawPortrait` to take `PORTRAITDEF` struct with embedded `PORTRAITFOG` substruct
- WinAPI-style ALL CAPS struct names with LP/LPC pointer typedefs

## What's Remaining

### OnUpdateModel Event Not Firing
**Problem:** The CharacterCreate model element has `hidden="true"` in XML. The XML draw code skips hidden elements entirely, so `UIWow_XMLDrawElementLayer` never runs for the model.

**Expected flow:**
1. `SetGlueScreen("charcreate")` shows CharacterCreate
2. `CharacterCreate:Show()` is called
3. `OnShow` fires → `CharacterCreate_OnShow()` → `GetAvailableRaces()` etc
4. `UpdateCustomizationScene()` fires `this:AdvanceTime()` via `OnUpdateModel` event

**Actual flow:**
- Hidden model elements are skipped in `UIWow_XMLDrawElementLayer`
- `OnUpdateModel` never fires
- `AdvanceTime` never increments the frame counter
- Model animation stays at frame 0

**Fix needed:**
- Either fire `OnUpdateModel` for hidden model elements during draw
- Or change CharacterCreate.xml to not have `hidden="true"` on the model

### Race/Class Button Clicks
**Problem:** Race buttons use `this:GetID()` in OnClick to get the race index, but clicking doesn't update the model.

**Suspected cause:**
- `CharacterRace_OnClick(id)` → `SetCharacterRace(id)` → `SetBackgroundModel(CharacterCreate, fileString)` → `SetCharCustomizeBackground(...)` 
- The model path updates but `OnUpdateModel` doesn't fire to reload and redraw

### Gender Icons Showing Empty
**Problem:** Gender buttons show as empty rather than male/female icons.

**Suspected cause:** 
- `CharacterCreateGenderButtonMale:LockHighlight()` works (clicking changes highlighted state)
- But the actual gender icon texture isn't being rendered
- May be related to button state rendering not using the same texture draw path

## Test Command
```
build/bin/openwow -data "data/world-of-warcraft" +r_module stdout +com_frame_limit 10 +menu_character_create
```

## Reference
- whoa-master: `src/glue/CCharacterCreation.cpp` and `src/glue/CCharacterCreationScript.cpp` show how the real WoW handles this
- Real WoW uses `CSimpleModelFFX` for the customize frame with proper `OnUpdateModel` handling
- Character component system manages the actual character model separate from background
