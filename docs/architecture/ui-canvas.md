# UI Canvas: Window Geometry, Widescreen Chrome, and Resizing

## Contract

The UI canvas is the engine layer between SDL window geometry and everything drawn in authored UI units.
`client/cl_canvas.c` owns it; nothing else queries the window to decide where UI goes.

| Value | Meaning | Owner |
|-------|---------|-------|
| `UICANVAS.scene` | Full UI scene in authored units (`UI_BASE_WIDTH` x `UI_BASE_HEIGHT` or wider). The renderer maps it onto the whole drawable (`re.SetUIScene` / `R_UISceneRect`). | client canvas |
| `UICANVAS.root` | Server-authored HUD root (`SCR_LayoutSceneRect`). Equal to the scene except under `UI_CANVAS_EXPAND_CENTER`, where it is the authored 4:3 area centered in the scene. | client canvas |
| `UICANVAS.window` | Logical window size the canvas was resolved from; `SCR_ScreenToUI` maps pointer pixels through it so a frame never mixes an old scene with a new window. | client canvas |
| `UICANVAS.policy` | `UI_CANVAS_STRETCH`, `UI_CANVAS_EXPAND` or `UI_CANVAS_EXPAND_CENTER`; resolved by `CL_GameCanvasPolicy()` from the mounted data. | per-game client hook |
| `UICANVAS.chrome` | `UI_CANVAS_STANDARD` or `UI_CANVAS_WIDE`: wide exactly when scene area exists beside the root. Reported to the game with the `ui_canvas` client command. | client canvas, mirrored in `client_s.canvas` |

`common/ui_canvas.h::UI_ResolveCanvas(window, policy)` is the single resolver. Aspects at or below `UI_MIN_ASPECT`
keep the authored scene (narrower windows squash it, as retail does). Wider windows stretch it (`STRETCH`) or widen it
to `UI_BASE_HEIGHT * aspect` (`EXPAND`, `EXPAND_CENTER`). Types live in `common/shared.h`; the per-game default is
`UI_CANVAS_POLICY` in `games/<game>/common/ui_constants.h` (SC2 and WoW: `EXPAND`; WC3: `STRETCH` for classic data).

### Why the policy is data-driven

Retail 1.30+ `UI\FrameDef\UI\ConsoleUI.fdf` authors `ConsoleTexture05`/`ConsoleTexture06` tiles anchored
`TOPLEFT,-0.256,0` / `TOPRIGHT,0.256,0` (and the same at the bottom): they hang 0.256 outside the 4:3 root on each
side and cover the sides up to about 2.19:1. Classic archives (1.2x) author none of them. Widening the canvas with
classic data leaves black margins beside the console (the complaint behind PR #497); stretching with 1.30+ data hides
the extension tiles and stretches the 4:3 skin (the regression PR #497 introduced). `CL_GameCanvasPolicy()` in
`games/warcraft-3/common/world_w3.c` therefore reads `ConsoleUI.fdf` through `FS_ReadFile` and selects
`EXPAND_CENTER` when `W3_FdfReferencesFile(text, "ConsoleTexture05")` finds the tile outside comments, otherwise the
classic `STRETCH`. It is resolved at `CL_CanvasInit` and again in `CL_RebuildMenu` (`menu_restart`), because a RoC/TFT
edition switch changes which archives are visible without remounting.

## Data Flow

```text
SDL window event ──► CL_WindowEvent ──► re.WindowChanged (drawable resync at R_BeginFrame)
                                    └─► CL_CanvasWindowChanged ──► UI_ResolveCanvas ──► re.SetUIScene
CL_Frame ──► CL_CanvasFrame(cl_realtime): resample (vid_apply, silent resizes), settle timer, ui_canvas commit
SCR_ClearLayer / CL_WindowRoot / SCR_DrawLoadingLayout ──► SCR_LayoutSceneRect() == canvas.root
SCR_ScreenToUI, SCR_ProjectWorldPoint, cinematic fade, SCR_UICanvasWidth ──► canvas.scene
menu UI_GetSceneRect, glue sprite layers ──► renderer->GetUISceneRect() (the pushed scene)
CL_SendBegin ──► CL_CanvasWriteChrome ──► clc_stringcmd "ui_canvas N" ──► "begin"
G_ClientCommand "ui_canvas" ──► client_s.canvas ──► G_RefreshResourceBar re-sends LAYER_CONSOLE
UI_WriteConsoleBackdrop ──► UI_SetHidden(hud.console_wide[i], canvas != WIDE) ──► UI_WriteFrameWithChildren
```

### Resizing without remounting

Layout is solved every frame from the retained `svc_layout` blobs against the live canvas, so an interactive resize
re-lays out continuously and nothing is torn down mid-drag. The only state that must not flap is what the server
authors: `CL_CanvasFrame` commits `canvas.settled` after the window size has held for `CL_CANVAS_SETTLE_MS`
(250 ms) and forwards `ui_canvas <class>` only when the settled class differs from the one the connection already
sent and the connection is `ca_active`. The timer starts at the first frame boundary that observes a new size
(`canvas.stable`), so a size that flips and flips back inside one SDL poll pass never restarts it and silent
resizes (`vid_apply`) are timed the same way as event-driven ones. Resizes within one class (16:9 to 16:10) cost no
round trip. Zero-sized windows (minimized, not yet mapped) keep the previous canvas. `UI canvas: <policy> policy` on
stderr records the resolved policy at start-up and after an edition switch.

The begin handshake is the exception: `CL_CanvasWriteChrome` commits the live window and writes `ui_canvas` on the
reliable channel immediately before `begin`, so `ClientBegin` authors the console for the window the client actually
has and a fullscreen transition still settling at startup does not cost a second authoring.

### Class-gated chrome and memory

`UI_LoadHudConsole` collects the `ConsoleUI` texture children whose skin key is `ConsoleTexture05`/`06` into
`hud.console_wide[]`. Each `UI_WriteConsoleBackdrop` hides them unless the recipient's `client_s.canvas` is wide, so a
standard client never receives those frames. Their skin keys are also the only keys `UI_LoadTexture` registers lazily:
parsing the FDF stores the key behind a handle `>= HUD_DEFERRED_IMAGE_BASE` (`hud.deferred_key[]`) and `UI_LiveImage`
resolves it through `UI_ThemeImagePath` and `gi.ImageIndex` the first time a wide client's console is written. Without
this, parsing `ConsoleUI.fdf` would put the tiles into `CS_IMAGES` for every session and every client would precache
them. `UI_ImageKey` returns the key for either handle range.

Once a wide client has written the console, the `CS_IMAGES` slots stay occupied until the next level (see
[hud-media.md](../games/warcraft-3/hud-media.md)); switching back to a standard window removes the frames but not the
registrations, and in multiplayer every client precaches art registered for any client.

## Schema

`ui_canvas <class>` is a reliable `clc_stringcmd` handled by `CMD_UICanvas` in `games/warcraft-3/game/g_commands.c`.
`<class>` is the `UICANVASCLASS` integer; anything outside `0..UI_CANVAS_CLASS_COUNT-1` or non-numeric is rejected
with a stderr line. `client_s.canvas` is runtime-only (`F_IGNORE` in `client_fields[]`); a reconnecting client
reports it again before `begin`. `client_s.resourcebar.canvas` caches the class the console was last authored for.

## Diagnostic Workflow

```sh
# Widescreen data: HUD centered with extension tiles; drag the window across 4:3 and back.
build/bin/openwarcraft3 +set vid_native 1 +set vid_fullscreen 1 +map Maps\Campaign\Orc03.w3m
# Classic data: the same launch stretches the authored scene (stderr: "WC3: ConsoleUI.fdf unavailable" only if the FDF is missing).
```

Evidence: `Video: drawable size changed` lines from the renderer, one `ui_canvas` command per settled class change
(`+set ui_layout_debug 1` shows the replaced `LAYER_CONSOLE` payload), and 05/06 tile configstrings appearing only after
the first wide console write.

## Known Pitfalls

- Do not compute the scene from `re.GetWindowSize()` or `tr.drawableSize` anywhere else; the drawable can round to a
  slightly different aspect than the logical window and the two would disagree about the class at exactly 4:3.
- Hidden frames are skipped by `UI_WriteFrameWithChildrenSizedToText`; the gate must run before every console write
  because the FRAMEDEF tree is shared by all recipients.
- `SV_Begin_f` assigns a missing edict, but `ui_canvas` needs one: it is sent after the configstring pages, which already
  assigned it in `SV_Configstrings_f`.
- `Cmd_ForwardToServer` prints "Unknown command" below `ca_active`; the canvas never calls it earlier.

## Verification

- `tests/test_ui_canvas.c`: resolver matrix for all policies and aspects; renderer keeps the pushed scene and rejects empty rects.
- `games/warcraft-3/tests/test_client_canvas.c`: pointer/layout/renderer agreement under every policy, retail console edge
  capture, world projection, settle-gated commits, activation, begin ordering, policy re-resolution, zero-sized windows.
- `games/warcraft-3/tests/test_menu_fdf.c::glue_sprite_layers_follow_widescreen_edges`: glue scene and sprite layers under both WC3 policies.
- `tests/test_net.c` cinematic fade and `UIFLAG_EXTEND_WIDESCREEN_X` under the centered policy.
- `games/warcraft-3/game/tests/t_canvas.c`: deferred tile registration, per-class console authoring against the fixture
  `TestUI/FrameDef/UI/ConsoleUI.fdf` (retail 1.30 layout) with per-race skin tiles, `ui_canvas` validation, and the
  re-send through `globals.RunFrame()`. `t_game.c` save round-trip proves the class is runtime-only.
- Gap: the actual on-screen drag behaviour on X11/Wayland compositors is a platform property; a bounded launch as above
  is the only way to see the intermediate frames of a live drag.

See also: [ui-system.md](ui-system.md), [client-windows.md](client-windows.md),
[hud-media.md](../games/warcraft-3/hud-media.md), [cinematics.md](../games/warcraft-3/cinematics.md).
