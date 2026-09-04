# Proposal: UI Libs as Frame-Tree Builders, Client Owns the Renderer

## Problem

Today WC3 and WoW menu screens call the renderer directly through `uiimport.GetRenderer()`. This means every UI library must know about the renderer API, hold a renderer reference, and call draw functions itself. The result is two separate rendering paths for menus and HUDs, and every new game UI lib must reimplement the same drawing logic.

SC2 already demonstrates a better model: its UI lib is a pure stub and the entire HUD is driven by `svc_layout` blobs rendered generically by `SCR_DrawLayout()` in the client. The goal of this proposal is to extend that model to **all** UI rendering — including menus — so UI libs only handle loading, layout resolution, and event callbacks.

## Current Architecture

```
[WC3/WoW menu]                      [WC3/WoW/SC2 in-game HUD]
  ui.Refresh()                         svc_layout (net)
    → UI_DrawFrameOne()                  → CL_ParseLayout()
        → re.DrawImageEx()                   → cl.layout[layer]
        → re.DrawText()                          → SCR_DrawLayout()
        → re.DrawBackdrop()                          → re.DrawImageEx()
                                                     → re.DrawText()
```

Two independent paths. The renderer lives in both the UI lib and the client.

## Proposed Architecture

The UI lib resolves layout and serializes to a flat `UIFRAME[]` blob, then hands it to the client via a new import function. The client renders it through the same `SCR_DrawLayout()` path used by the in-game HUD. The UI lib never touches the renderer.

```
[all UI: menu + HUD]
  ui.Refresh()                        svc_layout (net, in-game only)
    → anchor/layout solver               → CL_ParseLayout()
    → serialize UIFRAME[] blob                ↘
    → uiimport.SetLayout(layer, blob)    cl.layout[layer]
                                              ↓
                                         SCR_DrawLayout()
                                           → re.DrawImageEx()
                                           → re.DrawText()
                                           → re.DrawBackdrop()
```

One rendering path. The renderer lives only in the client.

## Required Changes

### 1. Add `SetLayout` to `uiImport_t` (`client/ui.h`)

```c
void (*SetLayout)(int layer, const void *data, size_t size);
```

The client implementation stores the blob into `cl.layout[layer]`, exactly the same slot that `CL_ParseLayout()` fills from the network. `SCR_DrawLayout()` is unaware of the source.

### 2. Move the layout solver out of draw calls

Currently `UI_LayoutRect()` and `UI_DrawFrameOne()` are interleaved in `menu_render.c`. The UI lib's per-frame work becomes:

1. Run the anchor/SetPoint solver (`UI_LayoutRect`) to compute screen-space rects — same as today.
2. Serialize the resolved frame tree to `UIFRAME[]` — the same format `hud_write.c` uses for in-game HUD.
3. Call `uiimport.SetLayout(layer, blob, size)`.
4. Return — no renderer calls.

### 3. Verify `UIFRAME` coverage for menu frame types

The `UIFRAME` wire format (`common/shared.h`) was designed for in-game HUDs. Before committing, audit whether it covers all WC3 menu frame types: backdrops, 9-slice borders, button states, portrait frames, and font/color variants. Extend the format if gaps exist — this is the only potentially load-bearing unknown.

### 4. Remove `GetRenderer` from `uiImport_t`

Once no UI lib calls `GetRenderer()`, remove it from the import table. This enforces the boundary — a UI lib physically cannot reach the renderer.

## Migration Order

1. **SC2** — already done (UI lib is a stub, all HUD via `svc_layout`).
2. **WC3** — add `SetLayout` import, rewrite `menu_render.c` to serialize instead of draw. Menu and HUD unify under `SCR_DrawLayout()`.
3. **WoW** — same as WC3; in-game presentation follows the client/server-authored gameplay path.

## What UI Libs Retain

- FDF/XML parsing and frame-tree construction.
- Anchor/SetPoint layout solver.
- Event callbacks: `KeyEvent`, `MouseEvent`, `TextInput`, `GameCommand`, `ShowWindow`.
- State machines for screen transitions.
- Asset loading requests via import table (`LoadTexture`, `LoadFont`).

They do **not** retain: a renderer reference, draw call dispatch, or any knowledge of screen resolution beyond layout units.

## Tradeoffs

| | Current | Proposed |
|---|---|---|
| Renderer access | UI lib + client | Client only |
| Menu rendering path | Direct draw calls in UI lib | `SCR_DrawLayout()` |
| HUD rendering path | `SCR_DrawLayout()` | `SCR_DrawLayout()` |
| Adding a new game UI | Must reimplement draw dispatch | Implement layout solver only |
| `UIFRAME` format scope | HUD subset | Must cover all frame types |
| Per-frame serialization cost | None (draw directly) | One blob encode per frame |

The serialization cost is small — the blob is already built by `hud_write.c` every frame for in-game HUDs with no noticeable overhead.
