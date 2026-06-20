# UI DLL / Client Unification — edict_s / entityState_t Pattern

## Motivation

Apply the Quake `game.dll` / client relationship to the UI layer:

| Quake pattern | UI analogy |
|---|---|
| `entityState_t` — shared, both sides know layout | `uiBaseFrame_t` in `common/shared.h` ✓ |
| `edict_s` — game-private extension, client never reads | `uiFrameDef_s` (WC3) / `uiWowXmlElem_t` (WoW) — embedded as first member ✓ |
| `entity_size` + `edicts` — client walks flat array by stride | **Missing** — client calls `DrawFrame()` as black box |
| Client owns rendering (`R_AddRefEntityToScene`) | **Missing** — `on_draw` lives inside the DLL |
| Single hit-test in client | **Missing** — duplicated per-game inside each DLL |
| Layout rect cache is a client/renderer concern | WC3 `runtimes[]` lives in the DLL |

The refactor exposes enough from `uiExport_t` for the client to own iteration, layout
caching, hit-testing, and rendering. The DLL handles only format-specific parsing
(FDF/XML), layout resolution, and widget-specific draw/event logic.

---

## Step 1 — Tighten `on_draw` / `on_event` to `LPUIBASEFRAME`

Change `void *` to typed pointer:

```c
void (*on_draw)(LPUIBASEFRAME frame, LPCRECT rect);
void (*on_event)(LPUIBASEFRAME frame, FLOAT x, FLOAT y, int button, BOOL down);
```

Game-specific handlers cast: `uiFrameDef_t *f = (uiFrameDef_t *)frame;`

Eliminates meaningless `void *` while keeping vtable-style dispatch.

---

## Step 2 — Reconcile WoW `EF_*` flags with `uiBaseFrame_t` fields

WoW's `elem.flags` still carries `EF_HIDDEN`, `EF_ENABLED`, `EF_USED` separately
from migrated `base.hidden` / `base.disabled`. Two sources of truth.

Migrate WoW to use `base.hidden` and `base.disabled` exclusively. Remove redundant
`EF_HIDDEN` / `EF_ENABLED` bits. Keep `EF_USED`, `EF_HAS_SIZE`, `EF_HAS_TEXCOORD`
etc. as WoW-specific parse-time flags with no base equivalent.

---

## Step 3 — Expose `frame_size`, `frames`, `num_frames` from `uiExport_t`

Add three fields to `uiExport_t` in `client/ui.h`:

```c
size_t  frame_size;   /* sizeof the game-specific frame struct (stride for iteration) */
void   *frames;       /* base pointer to the flat frame array */
DWORD   num_frames;   /* number of LIVE frames (not capacity) */
```

Each game fills these in `UI_GetAPI()`:
- WC3: `frame_size = sizeof(FRAMEDEF)`, `frames = frames_array`, `num_frames = <live count>`
- WoW: `frame_size = sizeof(uiWowXmlElem_t)`, `frames = elems`, `num_frames = count`

The client iterates:
```c
for (DWORD i = 0; i < ui.num_frames; i++) {
    LPUIBASEFRAME base = (LPUIBASEFRAME)((char*)ui.frames + i * ui.frame_size);
    if (base->ui_flags & UIFLAG_HIDDEN) continue;
    // layout, hit-test, draw...
}
```

---

## Step 4 — Add `ResolveParent` and `ResolveRect` callbacks to `uiExport_t`

The client can't generically resolve `void *parent` or compute screen rects without
understanding each game's layout system (WC3 has 3-point anchors, WoW has XML anchors).

```c
LPUIBASEFRAME (*ResolveParent)(LPUIBASEFRAME frame);
BOOL          (*ResolveRect)(LPUIBASEFRAME frame, RECT *out);
```

**ResolveParent**: WC3 returns `frame->Parent`, WoW does `elems[parent]`.
Used by client for parent-chain traversal (visibility checks, layout invalidation).

**ResolveRect**: WC3 calls `UI_LayoutRect`, WoW calls `UIWow_XmlComputeRect`.
Returns the computed screen-space AABB. Client caches the result.

---

## Step 5 — Move layout rect cache to client

WC3 maintains `runtimes[MAX_UI_CLASSES]` inside the DLL. This is a rendering concern —
it belongs in the client.

The DLL keeps anchor/size data in frame structs. The client calls `ResolveRect()`
to compute screen rects and caches them in a parallel array owned by the client.

Cache lifecycle:
- Client allocates `rect_cache[frame_count]`
- After DLL `Refresh()`, client rebuilds dirty rects via `ResolveRect()`
- Hit-test and draw read from the client-owned cache
- DLL no longer needs `runtimes[]`

---

## Step 6 — Move hit-test to client

`UI_HitTest` is duplicated in WC3 (`ui_render.c`) and WoW (`ui_xml.c`).

Once the client owns frames and rect cache, implement one generic hit-test:
```c
LPUIBASEFRAME CL_UIHitTest(FLOAT x, FLOAT y);
```

Walk the base array back-to-front, skip hidden/disabled, point-in-rect check
against the client-owned rect cache, return topmost frame with `on_event != NULL`.

Remove `HitTestLayout` from `uiExport_t` (folds into the same loop).

---

## Step 7 — Delete `DrawFrame` black box, client owns render loop

Once the client owns iteration, layout cache, and hit-test:

```c
void CL_UIDrawFrames(void) {
    for (DWORD i = 0; i < ui.num_frames; i++) {
        LPUIBASEFRAME base = (LPUIBASEFRAME)((char*)ui.frames + i * ui.frame_size);
        if (base->ui_flags & UIFLAG_HIDDEN) continue;
        RECT rect;
        if (!ui.ResolveRect(base, &rect)) continue;
        if (base->on_draw) base->on_draw(base, &rect);
    }
}
```

The DLL's `DrawFrame` implementation is deleted. The DLL retains `Refresh`
(per-tick logic: animations, Lua callbacks, timer events).

---

## Dirty flag for layout invalidation

After `Refresh()`, the DLL marks frames whose layout changed:
```c
#define UIFLAG_LAYOUT_DIRTY (1 << 5)
```

The client detects this and rebuilds the rect cache for those frames.

---

## Files touched

| File | Change |
|---|---|
| `common/shared.h` | Update `on_draw`/`on_event` signatures to `LPUIBASEFRAME`; add `UIFLAG_LAYOUT_DIRTY` |
| `client/ui.h` | Add `frame_size`, `frames`, `num_frames`, `ResolveParent`, `ResolveRect` to `uiExport_t`; remove `DrawFrame` (step 7) |
| `client/cl_main.c` | Implement `CL_UIDrawFrames`, `CL_UIHitTest`, client-owned rect cache |
| `games/warcraft-3/ui/ui_main.c` | Fill `frame_size`/`frames`/`num_frames`/callbacks in `UI_GetAPI`; delete `DrawFrame` export |
| `games/warcraft-3/ui/ui_render.c` | Remove `runtimes[]` and `UI_HitTest`; keep `UI_LayoutRect` as `ResolveRect` backend |
| `games/world-of-warcraft/ui/ui_main.c` | Fill exports; delete `DrawFrame` export |
| `games/world-of-warcraft/ui/ui_xml.c` | Migrate `EF_HIDDEN`/`EF_ENABLED` to base fields; expose `ResolveRect` |
