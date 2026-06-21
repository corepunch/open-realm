# Slim Down uiImport_t + Separate FDF UI from Server-Authored UI

## Architecture Principle

- **UI library** (`libui`) = FDF-authored UI only: FDF parsing, frame templates, layout solver, menu/checkbox/slider/popup controls, event dispatch
- **Server-authored UI** = client code: loading screens, in-game HUD (resource bars, minimap, command buttons, build queues, portraits, tooltips, status bars, overlays)
- Layout* callbacks should NOT be in uiImport_t — the client handles server-authored UI directly using `SCR_*` functions

## Plan

### Phase 1: Remove dead code (7 fields)
ModelIndex, ReadSheet, GetLoadingStatus, MapNameMatchesFile, SanitizeMapListField, RequestUnitUI, Error

### Phase 2: Remove GetClientTime (1 field)
Pass time through Refresh. WC3 uses ui_state.time, WoW uses wow_ui.time.

### Phase 3: Move server-authored UI from UI library to client
ui_layout.c entire file + server-authored parts of ui_main.c → client/cl_unit_ui.c

### Phase 4: Remove Layout callbacks (6 fields)
LayoutClear, LayoutNumFrames, LayoutFrame, LayoutRect, LayoutStringValue, LayoutDrawText

### Phase 5: Remove loading getters (3 fields)
GetLoadingMap, GetLoadingStatus, GetLoadingProgress

### Phase 6: Move map info callbacks to WC3 game library (13 fields)

### Phase 7: Move sheet/config to WC3 game library (remaining)

## Final uiImport_t: 52 → ~22 fields
