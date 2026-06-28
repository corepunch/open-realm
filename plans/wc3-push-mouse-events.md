# Push-Based Mouse Events for WC3 UI

## Problem
WC3 UI polls mouse state via `uiimport.GetMouseEvent()`, `GetMouseButton()`, `GetMouseButtonDown()` **inside draw functions**. Clicks, scrolls, focus changes, slider drags, and menu selections all happen during rendering — poor separation of concerns.

## Goal
Client pushes mouse events to UI during `SDL_PollEvent`. UI processes them immediately (hit test + action). Draw becomes pure rendering.

## Architecture

```
SDL_PollEvent → CL_Input() pushes to ui.MouseEvent(x, y, button, down)
                         ↓
              UI_MouseEventLocal() — hit test against cached layout rects
                         ↓
              Dispatch to controls: button click, checkbox toggle,
              slider drag, scroll, editbox focus, menu select
                         ↓
              UI_DrawFrame() — pure rendering, reads control state
```

Key insight: `UI_LayoutRect(frame)` returns **cached** rects from the previous frame's layout solve. Hit testing at event time uses these cached rects — they're accurate enough for interaction.

## Steps

### Step 1: Add hit testing infrastructure
- Add `UI_HitTest(FLOAT fdf_x, FLOAT fdf_y)` in `ui_render.c`
- Walks cached `runtimes[]` back-to-front, returns topmost interactive frame
- Add `UI_PointInFrame(FLOAT x, FLOAT y, LPCFRAMEDEF frame)` helper

### Step 2: Rewrite UI_MouseEventLocal to process events
- Convert pixel → FDF coords
- Hit test to find frame under cursor
- Route by frame type (button, checkbox, slider, editbox, maplist, popup)
- Delegate to `uiScreen_t.mouse_event`

### Step 3: Move button click handling from draw to event handler
- Remove click detection from `UI_DrawFrameOne` (lines 747-764)
- Event handler: on `LEFT_UP` over button → execute `OnClick`

### Step 4: Move checkbox toggle from draw to event handler
- Remove toggle from `UI_DrawCheckBox`
- Event handler: on `LEFT_UP` over checkbox → toggle `Checked`

### Step 5: Move slider interaction from draw to event handler
- Remove `UI_UpdateSliderInteraction` from draw path
- Event handler: `LEFT_DOWN` → start drag; `LEFT_UP` → end drag; motion → update value

### Step 6: Move editbox focus from draw to event handler
- Remove `UI_ClearEditFocusIfClickedOutside` from draw
- Remove focus-on-click from `UI_DrawFrameOne`
- Event handler: `LEFT_DOWN` over editbox → focus; `LEFT_DOWN` outside → clear

### Step 7: Move maplist interaction from draw to event handler
- Remove selection and scroll handling from `UI_DrawMapListControl`
- Event handler: `LEFT_UP` → select row; wheel → scroll

### Step 8: Move popup menu interaction from draw to event handler
- Remove `UI_ClosePopupIfClickedOutside` from draw
- Remove scroll and item selection from `UI_DrawMenu`
- Event handler: `LEFT_DOWN` outside → close; wheel → scroll; `LEFT_UP` item → select

### Step 9: Remove pull accessors from uiImport_t
- Remove `GetMouseFdf`, `GetMousePos`, `GetMouseButton`, `GetMouseButtonDown`, `GetMouseEvent`
- Remove `CL_UIGetMouse*` implementations from `cl_main.c`
- Remove `uiClientMouseEvent_t` enum

### Step 10: Update tests
- Tests that set `test_mouse_event` and call `UI_DrawFrame` need to call `UI_MouseEventLocal` instead
- Update mock state accordingly

### Step 11: Clean up
- Remove dead code
- Update AGENTS.md with push event pattern
- Verify build and rendering
