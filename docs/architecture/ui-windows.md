# UI Windows — Proposal

## Problem

Server-authored layouts today fill the entire screen. Every root frame in a `svc_layout`
payload must carry an anchor that positions it relative to the scene root (0, 0, BASE_W,
BASE_H). This creates two pain points:

1. **Anchor-chain fragility.** The client layout resolver (`cl_layout.c`) only follows
   `FPP_MIN` (top/left) anchor chains to compute a frame's absolute position. A
   `FRAMEPOINT_BOTTOM`-only anchor returns y = 0, placing the frame at the wrong position.
   Workaround today: every server-authored parent container must use an explicit
   `FRAMEPOINT_TOPLEFT` anchor with pre-computed inline coordinates — a pattern that
   recreates what `UI_SetFrameRect` did before, just without named `#define` constants.

2. **No floating windows for WoW.** WoW has independent panels (inventory bag, chat box,
   quest log, spellbook) that exist at different screen positions and can overlap. Today,
   the FrameXML layer handles these client-side. For server-authored WoW HUD elements
   (buff bar, target frame, resource bars) it would be cleaner to send a bounded "window"
   once and keep layout frames relative to that window, rather than repeating screen-space
   anchors on every root frame.

## Proposal

Add a **window descriptor** (`uiWindow_t`) at the start of each `svc_layout` layer
payload, before any frame data. A window is a named, bounded rect:

```c
typedef struct {
    UINAME  name;           /* stable identifier, e.g. "WC3InfoPanel" */
    RECT    rect;           /* x, y, w, h in UI coordinate space       */
    BYTE    layer;          /* same layer ID as svc_layout uses today   */
} uiWindow_t;
```

The client registers the window and sizes its root clip rect accordingly. All frames in
the subsequent `svc_layout` payload are resolved relative to `(0, 0, rect.w, rect.h)` —
so parent frames can use `FRAMEPOINT_TOPLEFT, 0, 0` (fill the window) or sub-anchors
without knowing screen-space coordinates.

```
Server                                    Client
──────                                    ──────
svc_layout_window "WC3InfoPanel"
  rect = { 0.310, 0.480, 0.180, 0.120 }  ←  client allocates/repositions the window
  (frames follow, relative to window)
```

### Window lifetime

- Windows are **persistent across frames** — the rect is sent once and cached on the
  client. Subsequent `svc_layout` payloads that reference the same window name skip the
  rect header and jump straight to frame data.
- A window with `rect = {0, 0, 0, 0}` hides/destroys it.
- Windows with the same name on different layers are independent.

### WC3 panels

Each logical HUD region becomes a window:

| Window name          | Rect (x, y, w, h)                    | Layer             |
|----------------------|---------------------------------------|-------------------|
| `WC3InfoPanel`       | 0.310, 0.480, 0.180, 0.120           | `LAYER_INFOPANEL` |
| `WC3CommandBar`      | 0.617, 0.087, 0.180, 0.130           | `LAYER_COMMANDBAR`|
| `WC3Portrait`        | 0.215, 0.486, 0.080, 0.080           | `LAYER_CONSOLE`   |
| `WC3Minimap`         | 0.007, 0.453, 0.140, 0.140           | `LAYER_CONSOLE`   |
| `WC3Tooltip`         | 0.580, 0.340, 0.220, 0.100           | `LAYER_TOOLTIP`   |
| `WC3QuestDialog`     | 0.150, 0.070, 0.500, 0.405           | `LAYER_DIALOG`    |

Frames within each layer use `SetAllPoints` or relative anchors — no screen-space
literals appear in the C code that populates them.

### WoW windows

WoW server-authored HUD elements (target frame, buff/debuff row, cast bar, resource bars)
can be sent as windows. Each window is a standalone panel:

```c
UI_SendWindow("WoWTargetFrame",  MAKE(RECT, 0.30f, 0.02f, 0.20f, 0.10f), LAYER_HUD);
UI_SendWindow("WoWBuffRow",      MAKE(RECT, 0.65f, 0.00f, 0.35f, 0.05f), LAYER_HUD);
UI_SendWindow("WoWCastBar",      MAKE(RECT, 0.30f, 0.88f, 0.40f, 0.04f), LAYER_HUD);
```

Client-side FrameXML panels (inventory, chat, quest log) remain handled by the WoW UI
layer and do not go through `svc_layout`. The window mechanism is exclusively for panels
whose content is server-authored.

## Client-side changes

- `cl_layout.c`: read `uiWindow_t` header when present; store rect in a per-name window
  table; set the origin offset applied to all frame absolute positions in that batch.
- The `scr_frame_abs_y` / `scr_frame_abs_x` resolvers work unchanged — they already
  return coordinates relative to the layer's coordinate space. The window offset is
  added once when the resolved rect is written to `runtimes[]`.

## Migration path

1. Keep the current `FRAMEPOINT_TOPLEFT` inline approach as a fallback for layers that
   do not declare a window — no immediate breakage.
2. Convert WC3 HUD layers one at a time: introduce `UI_SendWindow` in `hud_write.c` and
   update the layer's root frames to use `SetAllPoints` / `FRAMEPOINT_TOPLEFT, 0, 0`.
3. Add WoW window sends when server-authored WoW HUD panels are authored.

## What this is not

This is not a full windowing system. Windows do not overlap, z-order, or respond to
clicks as a unit. They are layout scopes — named rects that let the server author content
without embedding screen-space coordinates into every frame tree.
