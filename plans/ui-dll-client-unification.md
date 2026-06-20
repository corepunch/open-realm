# UI DLL / Client Unification — edict_r / entityState_t Pattern

## Motivation

The goal is to apply the Quake `game.dll` / client relationship to our UI layer:

| Quake pattern | UI analogy |
|---|---|
| `entityState_t` — shared, both sides know layout | `uiBaseFrame_t` in `common/shared.h` ✓ |
| `edict_r` — server-private extension, client never reads | `uiFrameDef_s` (WC3) / `uiWowXmlElem_t` (WoW) — embedded as first member ✓ |
| `entity_size` — client walks flat edict array by stride | **Missing** — client calls `DrawFrame()` as a black box |
| Client owns rendering (R_AddRefEntityToScene) | **Missing** — `on_draw` lives inside the dll |
| Single hit-test in client | **Missing** — duplicated per-game inside each dll |
| Layout rect cache is a client/renderer concern | WC3 `runtimes[]` lives in the dll |

Currently `uiExport_t.DrawFrame()` is a black box — the dll owns rendering, hit-test,
and layout. The refactor exposes enough from `uiExport_t` for the client to own those
responsibilities, leaving the dll to handle only file format parsing and widget-specific
draw/event logic.

---

## Step 1 — Expose `frame_size`, `num_frames`, `frames` from `uiExport_t`

Add three fields to `uiExport_t` in `client/ui.h`:

```c
size_t  frame_size;   /* sizeof the game-specific frame struct (stride for iteration) */
void   *frames;       /* base pointer to the flat frame array */
DWORD   num_frames;   /* total allocated slots */
```

Each game fills these in `UI_GetAPI()`:
- WC3: `frame_size = sizeof(uiFrameDef_t)`, `frames = frames_array`, `num_frames = MAX_UI_CLASSES`
- WoW: `frame_size = sizeof(uiWowXmlElem_t)`, `frames = wow_xml.elems`, `num_frames = wow_xml.count`

The client can then iterate:
```c
for (DWORD i = 0; i < ui.num_frames; i++) {
    LPUIBASEFRAME base = (LPUIBASEFRAME)((char*)ui.frames + i * ui.frame_size);
    if (base->hidden) continue;
    // layout, hit-test, draw...
}
```

---

## Step 2 — Move hit-test to the client

`UI_HitTest` is duplicated in WC3 (`ui_render.c`) and WoW (`ui_xml.c`). Both only need
`uiBaseFrame_t` fields (`hidden`, `disabled`, `on_event`) plus a computed rect.

Once the client iterates frames by stride, it can implement one generic hit-test:
```c
LPUIBASEFRAME CL_UIHitTest(FLOAT x, FLOAT y);
```

Walk the base array back-to-front, skip `hidden`/`disabled`, point-in-rect check against
the client-owned layout rect cache, return topmost frame with `on_event != NULL`.

Remove `HitTestLayout` from `uiExport_t` (currently only used for layout-layer frames —
that can fold into the same loop).

---

## Step 3 — Move layout rect cache (`runtimes[]`) to the client

Currently WC3 maintains `runtimes[MAX_UI_CLASSES]` of computed screen rects inside the
dll. This is a rendering/layout concern — it belongs in the client.

The dll keeps anchor/size data in the frame structs. The client resolves final screen rects
(calling back via a `uiImport_t` helper or doing it generically from `base.size` and
parent chain) and caches them in a parallel array owned by the client.

WoW has its own equivalent per-frame rect computation — unifies under the same cache.

---

## Step 4 — Tighten `on_draw` / `on_event` signature

Change `void*` parameter to `LPUIBASEFRAME`:

```c
void (*on_draw)(LPUIBASEFRAME frame, LPCRECT rect);
void (*on_event)(LPUIBASEFRAME frame, FLOAT x, FLOAT y, int button, BOOL down);
```

Game-specific handlers cast down: `uiFrameDef_t *f = (uiFrameDef_t *)frame;`

Eliminates the meaningless `void*` while keeping the same vtable-style dispatch.

---

## Step 5 — Reconcile WoW `EF_*` flags with `uiBaseFrame_t` fields

WoW's `elem.flags` still carries `EF_HIDDEN`, `EF_ENABLED`, `EF_USED` etc. separately
from the migrated `base.hidden` / `base.disabled` fields. This means two sources of truth.

Migrate WoW to use `base.hidden` and `base.disabled` exclusively, then remove the
redundant `EF_HIDDEN` / `EF_ENABLED` bits from `EF_*`. Keep `EF_USED`, `EF_HAS_SIZE`,
`EF_HAS_TEXCOORD` etc. as they are WoW-specific parse-time flags with no base equivalent.

---

## Step 6 — Remove `DrawFrame` black box from `uiExport_t`

Once the client owns iteration, layout cache, and hit-test, `uiExport_t.DrawFrame()` becomes:
- Iterate frames by stride
- For each visible frame: resolve rect from client cache, call `base.on_draw(base, rect)`

The dll's `DrawFrame` implementation can be deleted. The client calls `on_draw` directly
through the base pointer.

The dll retains `Refresh` (per-tick logic: animations, Lua callbacks, timer events).

---

## Order of execution

1. `frame_size` + `frames` + `num_frames` in `uiExport_t` (unblocks everything)
2. Tighten `on_draw`/`on_event` to `LPUIBASEFRAME`
3. Reconcile WoW `EF_HIDDEN`/`EF_ENABLED` → `base.hidden`/`base.disabled`
4. Move layout rect cache to client
5. Move hit-test to client (`CL_UIHitTest`)
6. Delete `DrawFrame` black box, client owns the render loop

---

## Files touched

| File | Change |
|---|---|
| `client/ui.h` | Add `frame_size`, `frames`, `num_frames` to `uiExport_t`; tighten FP signatures; remove `DrawFrame` (step 6) |
| `client/cl_main.c` | Implement client-side frame iteration, layout cache, hit-test |
| `common/shared.h` | Update `on_draw`/`on_event` signatures |
| `games/warcraft-3/ui/ui_main.c` | Fill `frame_size`/`frames`/`num_frames` in `UI_GetAPI`; delete `DrawFrame` export |
| `games/warcraft-3/ui/ui_render.c` | Remove `runtimes[]` and `UI_HitTest` once client owns them |
| `games/world-of-warcraft/ui/ui_main.c` | Fill `frame_size`/`frames`/`num_frames`; delete `DrawFrame` export |
| `games/world-of-warcraft/ui/ui_xml.c` | Migrate `EF_HIDDEN`/`EF_ENABLED` to base fields |
