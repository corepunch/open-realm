# ScrollView Clipping, Scrollbar Interaction, and Mouse Wheel

## Problem

The WoW XML UI parses `<ScrollFrame>` and `<Slider>` but treats them as plain `<Frame>` — no scroll offset, no child clipping, no scrollbar interaction, no mouse wheel support. The scroll Lua methods are all noops. There is zero scissor/stencil clipping in the XML draw path.

## Architecture

The renderer already supports scissor clipping via `drawImage_t.hasClip`/`clip` and `drawText_t.hasClip`/`clip`, flowing through `R_DrawImageBatch` → `R_SetUIClipScissor` → `GL_SCISSOR_TEST`. No stencil buffer needed.

## Changes

### 1. `ELEM_ON_MOUSE_WHEEL` script type
- Add to `uiWowXmlStr_t` enum
- Parse `<OnMouseWheel>` in `UIWow_XmlReadScripts`

### 2. ScrollFrame detection and scroll state
- Add `EF_IS_SCROLLFRAME` flag
- Detect `<ScrollFrame>` nodes during parse, set flag
- Add parallel `scroll[]` array: `scroll_y`, `scroll_range`, `scroll_child`

### 3. Lua method implementations
- Replace noop `SetVerticalScroll`/`GetVerticalScroll` with real read/write

### 4. Scissor clipping in `UIWow_XMLDraw`
- Track current clip state in draw loop
- When entering ScrollFrame descendants, set `hasClip`/`clip`
- Offset child Y by `-scroll_y`

### 5. Scrollbar rendering
- Draw track, thumb, up/down buttons for ScrollBar children
- Position thumb by `scroll_y / scroll_range`

### 6. Scrollbar interaction
- Extend hit-test for Thumb/UpButton/DownButton
- Thumb drag: track mouse delta, update scroll value
- Up/Down buttons: step scroll

### 7. Mouse wheel
- In `UIWow_XMLMouseEvent`, button 4/5 → hit-test ScrollFrames, scroll by step

### 8. OnMouseWheel script dispatch
- Run Lua `OnMouseWheel` when scroll event occurs

## Files Modified

- `games/world-of-warcraft/ui/ui_xml.c`

## Execution Order

1. Add `ELEM_ON_MOUSE_WHEEL` to string enum and parser
2. Add `EF_IS_SCROLLFRAME` flag, detect ScrollFrame nodes
3. Add parallel `scroll[]` array
4. Implement `SetVerticalScroll`/`GetVerticalScroll` Lua methods
5. Add scissor clipping + child Y offset in `UIWow_XMLDraw`
6. Add scrollbar basic rendering
7. Extend hit-test for ScrollBar parts
8. Add mouse wheel dispatch
9. Add scrollbar thumb drag interaction
10. Add OnMouseWheel script dispatch
