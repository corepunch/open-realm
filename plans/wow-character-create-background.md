# WoW Character Create Background Scene — Implementation Plan

## Context

After the recent addition of background scene rendering for WoW glue screens, the character creation screen's background needs to respond to race selection. The Lua code is already complete (in `GlueParent.lua` and `CharacterCreate.lua`); the C stubs are missing.

## Current State

### Lua (Already Done — No Changes Needed)
- `SetBackgroundModel()` in `GlueParent.lua:176-205` correctly routes to either `SetCharCustomizeBackground` (CharacterCreate) or `SetCharSelectBackground` (CharacterSelect)
- `CharacterCreate.lua:241` calls `SetBackgroundModel(CharacterCreate, fileString)` when race changes
- `CharacterCreate.lua:294` calls `UpdateCustomizationScene()` each frame

### C Stubs Missing

| Function | CharacterSelect | CharacterCreate | Status |
|---|---|---|---|
| `SetCharSelectModelFrame` / `SetCharCustomizeFrame` | ✅ Implemented | ❌ Stub (`UIWow_LuaNoop`) | **Missing** |
| `SetCharSelectBackground` / `SetCharCustomizeBackground` | ✅ Implemented | ❌ Stub (`UIWow_LuaNoop`) | **Missing** |
| `UpdateCustomizationScene` | N/A | ❌ Not in binding table | **Missing** |
| `AdvanceTime` | N/A | ⚠️ No-op | **Missing** |
| `SetFogColor/SetFogNear/SetFogFar` | N/A | ⚠️ No-op | **Missing** |

## Implementation

### File: `games/world-of-warcraft/ui/ui_lua.c`

#### 1. Implement `UIWow_LuaSetCharCustomizeFrame`
Mirror `UIWow_LuaSetCharSelectModelFrame` (line 489-496):
```c
static int UIWow_LuaSetCharCustomizeFrame(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    int idx = UIWow_XmlFindByNamePub(name);
    if (idx >= 0) {
        wow_ui.model_frame_idx = idx;
    }
    return 0;
}
```

#### 2. Implement `UIWow_LuaSetCharCustomizeBackground`
Mirror `UIWow_LuaSetCharSelectBackground` (line 498-505):
```c
static int UIWow_LuaSetCharCustomizeBackground(lua_State *L) {
    LPCSTR model_path = luaL_checkstring(L, 1);
    int idx = wow_ui.model_frame_idx;
    if (idx >= 0) {
        UIWow_XmlSetFrameModel(idx, model_path);
    }
    return 0;
}
```

#### 3. Add `UpdateCustomizationScene` stub
```c
static int UIWow_LuaUpdateCustomizationScene(lua_State *L) { (void)L; return 0; }
```

#### 4. Implement `AdvanceTime` — animate character preview
The current `UIWow_LuaFrameAdvanceTime` (ui_xml.c:585) is a no-op. We need to:
- Find the model element from the frame
- Increment its frame counter
- The `uiWowXmlElem_t` struct has no frame counter, so we need to add one OR pass time through the render entity

**Approach:** Add a `DWORD frame` field to `uiWowXmlElem_t` and increment it on each `AdvanceTime` call. The XML draw code at `ui_xml.c:1619-1622` uses the frame for animation. We need to track per-element frame counters.

Alternatively, we can pass time through `wow_ui` state and have the draw code use `tr.viewDef.time` for the animation. But the model needs to know which frame/entity to update.

**Better approach:** Store the frame in the elem struct and pass it to `DrawPortrait`. Modify:
- `uiWowXmlElem_t`: add `DWORD frame` field (default 0)
- `UIWow_LuaFrameAdvanceTime`: increment `e->frame` for the model frame
- `UIWow_XMLDrawElementLayer` line 1621: pass `e->frame` to `DrawPortrait`

But `DrawPortrait` signature is `void R_GameDrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim)` — it takes an anim name string, not a frame number. The M2 renderer uses `tr.viewDef.time` internally for animation.

Looking at `M2_RenderModel` and `M2_AnimationTime` in `r_m2.c`, the animation time comes from `entity->frame` or `tr.viewDef.time`. The frame counter in `renderEntity_t` drives animation.

**Solution:** We need to pass a frame index through to the render call. Add a `DWORD render_frame` field to `uiWowXmlElem_t`. In `AdvanceTime`, increment it. In the draw code, copy it to a local `renderEntity_t` and set `entity.frame = e->render_frame`.

But `DrawPortrait` doesn't take a renderEntity — it creates one internally (r_game.c:321-368). We need to either:
A) Add frame parameter to `DrawPortrait`
B) Store a `renderEntity_t` in the elem and have it passed to `DrawPortrait`
C) Store a "last advance time" timestamp and compute frame from elapsed time

**Chosen approach (A):** Extend `DrawPortrait` signature to take a frame index, and update the call site in XML drawing code.

#### 5. Implement Fog Support
The `UIWow_LuaFrameSetFog*` functions (ui_xml.c:566-569) are no-ops returning 0. We need to:
- Add fog color/near/far state to `uiWowXmlElem_t`
- Store fog params on the model element when `SetFogColor/SetFogNear/SetFogFar` are called
- Apply fog in the draw code (pass to renderer)

**Approach:** Add fog fields to `uiWowXmlElem_t`:
```c
COLOR32 fog_color;
FLOAT fog_near;
FLOAT fog_far;
BOOL has_fog;
```
- `SetFogColor` sets `fog_color` and `has_fog = true`
- `SetFogNear`/`SetFogFar` set fog near/far
- `ClearFog` sets `has_fog = false`
- In draw code: if `has_fog`, configure the renderer's fog state before drawing the model

But `DrawPortrait` uses a private `renderEntity_t` with no fog fields. The M2 renderer hardcodes fog-of-war texture lookup in the shader (r_m2.c:488-490). For character create background, we want scene-level fog.

**Simplification:** For now, set fog via the `renderEntity_t` flags or pass fog params to `DrawPortrait`. We can extend `DrawPortrait` to take fog params.

Actually, looking at `R_GameDrawPortrait` (r_game.c:320-406), it sets up a completely separate view/matrix setup. It clears depth buffer but doesn't configure fog. The M2 shader uses `uFogOfWar` sampler for fog-of-war (game world), not scene fog.

For the character create background, we need scene fog which is different from fog-of-war. The glue M2 models should render with race-specific atmospheric fog.

**Approach:** Add fog params to `DrawPortrait`. In `R_GameDrawPortrait`, set up fog color/near/far before rendering the M2. The M2 renderer needs to support scene fog in addition to fog-of-war.

This requires changes to:
1. `uiWowXmlElem_t` — add fog state
2. `UIWow_LuaFrameSetFogColor/Near/Far/ClearFog` — store fog state
3. `DrawPortrait` signature — add fog params
4. `UIWow_XMLDrawElementLayer` — pass fog to renderer
5. `R_GameDrawPortrait` — apply fog before M2 render
6. M2 shader — conditionally apply scene fog vs fog-of-war

**Deferred:** Fog support is non-trivial. Implement background switching first, then tackle fog separately.

## Modified Files

- `games/world-of-warcraft/ui/ui_lua.c` — implement 3 functions, add 2 bindings
- `games/world-of-warcraft/ui/ui_xml.c` — add `frame` field to elem, implement AdvanceTime, pass frame to DrawPortrait
- `games/world-of-warcraft/ui/ui_local.h` — declare new functions
- `games/world-of-warcraft/renderer/r_game.h` — extend `DrawPortrait` signature
- `games/world-of-warcraft/renderer/r_game.c` — apply fog in DrawPortrait
- `games/world-of-warcraft/renderer/m2/r_m2.c` — add scene fog support to shader

## Verification

1. Build: `make`
2. Run character create: `make run-ui-text UI_CMD=menu_character_create`
3. Check that no Lua errors appear and background loads
4. Verify character preview animates (AdvanceTime working)
