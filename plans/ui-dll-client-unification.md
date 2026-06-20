# UI DLL / Client Unification — edict_s / entityState_t Pattern

## Motivation

Apply the Quake `game.dll` / client relationship to the UI layer:

| Quake pattern | UI analogy |
|---|---|
| `entityState_t` — shared, both sides know layout | `uiBaseFrame_t` in `common/shared.h` ✓ |
| `edict_s` — game-private extension, client never reads | `uiFrameDef_s` (WC3) / `uiWowXmlElem_t` (WoW) — embedded as first member ✓ |
| `entity_size` + `edicts` — client walks flat array by stride | **Missing** — client calls `DrawFrame()` as black box |
| Client owns rendering (`R_AddRefEntityToScene`) | **Missing** — `on_draw` lives inside the DLL |
| `entityState_t.origin` — DLL writes, client reads | **Missing** — rects computed inside DLL draw path |
| Single hit-test in client | **Missing** — duplicated per-game inside each DLL |

The DLL writes data into `uiBaseFrame_t`. The client reads it and renders. No callbacks.

---

## Step 1 — Tighten `on_draw` / `on_event` to `LPUIBASEFRAME`

```c
void (*on_draw)(LPUIBASEFRAME frame, LPCRECT rect);
void (*on_event)(LPUIBASEFRAME frame, FLOAT x, FLOAT y, int button, BOOL down);
```

---

## Step 2 — Reconcile WoW `EF_*` flags with `uiBaseFrame_t` fields

Migrate WoW `EF_HIDDEN` / `EF_ENABLED` → `base.hidden` / `base.disabled`.
Keep `EF_USED`, `EF_HAS_SIZE`, `EF_HAS_TEXCOORD` as WoW-specific.

---

## Step 3 — Add `parent_index` and `screen_rect` to `uiBaseFrame_t`

```c
DWORD parent_index;  /* index into frames array, -1 = root */
RECT  screen_rect;   /* computed screen-space AABB, filled by DLL during Refresh */
```

The DLL computes `screen_rect` during `Refresh()` and writes it.
The client reads it directly — no callback needed.

Parent is an index, not a pointer. The client resolves `frames[parent_index]` itself.

---

## Step 4 — Expose `frame_size`, `frames`, `num_frames` from `uiExport_t`

```c
size_t  frame_size;   /* sizeof game-specific frame struct (stride) */
void   *frames;       /* base pointer to flat frame array */
DWORD   num_frames;   /* number of LIVE frames (not capacity) */
```

Client iterates:
```c
for (DWORD i = 0; i < ui.num_frames; i++) {
    LPUIBASEFRAME base = (LPUIBASEFRAME)((char*)ui.frames + i * ui.frame_size);
    if (base->ui_flags & UIFLAG_HIDDEN) continue;
    // use base->screen_rect directly
}
```

---

## Step 5 — Move hit-test to client

```c
LPUIBASEFRAME CL_UIHitTest(FLOAT x, FLOAT y);
```

Walk back-to-front, skip hidden/disabled, point-in-rect against `base->screen_rect`,
return topmost frame with `on_event != NULL`.

Remove `HitTestLayout` from `uiExport_t`.

---

## Step 6 — Delete `DrawFrame` black box, client owns render loop

```c
void CL_UIDrawFrames(void) {
    for (DWORD i = 0; i < ui.num_frames; i++) {
        LPUIBASEFRAME base = ...;
        if (base->ui_flags & UIFLAG_HIDDEN) continue;
        if (base->on_draw) base->on_draw(base, &base->screen_rect);
    }
}
```

DLL retains `Refresh()` (animations, Lua, timers). During `Refresh()`, DLL computes
`screen_rect` for each visible frame and writes it into the base struct.

---

## Dirty flag for layout invalidation

DLL sets `UIFLAG_LAYOUT_DIRTY` in `base.ui_flags` during `Refresh()` when a frame's
layout changes. Client can use this to invalidate caches or trigger re-layout.

---

## Files touched

| File | Change |
|---|---|
| `common/shared.h` | Update signatures; add `parent_index`, `screen_rect` to `uiBaseFrame_t` |
| `client/ui.h` | Add `frame_size`, `frames`, `num_frames` to `uiExport_t`; remove `DrawFrame` |
| `client/cl_main.c` | Implement `CL_UIDrawFrames`, `CL_UIHitTest` |
| `games/warcraft-3/ui/ui_main.c` | Fill exports; delete `DrawFrame` |
| `games/warcraft-3/ui/ui_render.c` | Remove `runtimes[]`; `UI_LayoutRect` writes `base->screen_rect` |
| `games/world-of-warcraft/ui/ui_main.c` | Fill exports; delete `DrawFrame` |
| `games/world-of-warcraft/ui/ui_xml.c` | Migrate `EF_HIDDEN`/`EF_ENABLED`; compute `screen_rect` |
