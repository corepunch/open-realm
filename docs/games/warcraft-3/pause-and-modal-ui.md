# Warcraft III Pause And Modal UI

## Contract

OpenRealm separates application/network progress from deterministic Warcraft III simulation time.

- `server/sv_main.c` owns the authoritative simulation pause gate. `SV_SetPaused(true)` stops `SV_RunGameFrame()`; it does **not** stop `SV_ReadPackets()` or client transport.
- `SV_SetPaused` publishes the authoritative state through the shared `paused` cvar, matching Quake II's client/server pause contract without widening snapshot structs.
- `games/warcraft-3/game/` owns Warcraft pause policy. `PauseGame` and single-client Quest-dialog ownership are combined before calling the generic `game_import.SetPaused` hook.
- `client/cl_window.c` owns transient server-authored modal windows such as Quest, Menu, Allies, Log, and Game Result. A topmost modal consumes input outside itself before persistent HUD/world handlers run.
- `client/cl_input_w3.c` owns WC3 camera/selection gestures. A modal cancels active pan, minimap drag, selection drag, hover, arrow scrolling, and edge scrolling.
- `client/cl_view.c` keeps submitting the cached world while `paused`, but does not rebuild the scene and sets `viewDef.deltaTime` to zero. UI and transport remain live while world animation and model-owned effects stay frozen.

Do not implement global pause by setting every unit's `paused` flag or by pausing every JASS timer. Those are object-level mechanics with different semantics.

## Server Data Flow

```text
PauseGame(true) / single-client Quest dialog opens
    -> G_SetScriptPaused / G_SetQuestDialogOpen
    -> G_RefreshPauseState
    -> gi.SetPaused(true)
    -> SV_SetPaused(true)
    -> SV_Frame keeps reading packets
       but does not call SV_RunGameFrame
```

For client-owned pause-owning modal windows, serialization precedes pause acquisition:

```text
UI_WriteWindow -> client parses and opens svc_window
    -> first modal without UI_WINDOW_NO_PAUSE sends pause 1
    -> G_SetClientModal(..., WC3_MODAL_CLIENT, true)
    -> G_RefreshPauseState
```

A window may combine `UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE` when it must capture world/camera input without freezing the
simulation. The Warcraft III Allies dialog and the result fallback use that combination; the latter preserves the existing
script-owned `PauseGame(true)` result lifecycle rather than acquiring a second client pause. Pause synchronization scans all
windows for a remaining pause-owning modal rather than assuming the topmost modal owns pause.

`UI_WINDOW_NO_ESCAPE` is separate from modal/pause ownership. The result window uses it so Escape is consumed without closing
the fallback; Continue/Load/Restart/Quit must run their explicit continuation commands before the window disappears.

Do not acquire the modal pause owner in the command that writes the window. The local client shares the authoritative
`paused` cvar and can observe it before reliable window delivery; modal-list synchronization makes the popup itself the pause
boundary. Closing a modal recomputes the list and sends `pause 0` only after the last modal is gone, matching Quake II's menu
stack lifecycle.

Modal FDF containing `DecorateFileNames` art must be serialized between `UI_SetCurrentClient(client)` and
`UI_SetCurrentClient(NULL)`. Esc-menu button backgrounds, borders, and highlights are symbolic war3skins keys; without the
recipient's race context they resolve to empty art even though the window packet, geometry, and pause handshake are valid.

`EscMenuMainPanel.fdf` declares `EscMenuBackdrop` outside `EscMenuMainPanel` because Blizzard's Esc-menu controller creates
and parents it at runtime. `EscMenuMainPanel` is authored with `SetAllPoints`, while the active `MainPanel` owns the assigned
`0.288 x 0.384` content size. Copy that size to `EscMenuMainPanel` and center it before serialization; otherwise title and
button anchors resolve against the full client canvas even though the separately sized decoration is correct. Center the
backdrop under that bounded controller and parent `MainPanel` beneath it so backdrop wire/draw order precedes controls. Hide
`EndGamePanel`, `ConfirmQuitPanel`, `HelpPanel`, and `TipsPanel` when opening the main menu; each is a sibling subtree and
otherwise overlaps the visible main panel.

FDF label offsets are serialized as `SHORT` values scaled by `UI_FRAMEPOINT_SCALE`, matching frame-point offsets. Decode the
scale in `SCR_LayoutDrawString`; adding the raw integer moves inherited Esc/Quest button labels tens of UI units off-screen,
while zero-offset titles continue to render and disguise the shared text-path regression.

While paused, `sv.time` and `sv.framenum` remain frozen. At `FRAMETIME` cadence the server still sends a snapshot packet with the frozen frame/time. This traffic is deliberate: `client/cl_main.c` disconnects after `CL_TIMEOUT_MSEC` (10 seconds) without a server packet, so returning from `SV_Frame` before transport would turn a long pause into `Connection to host timed out.`

On resume, `SV_SetPaused(false)` rebases only `sv.next_frame_msec` to the current monotonic `svs.realtime`. Wall-clock time spent paused is therefore discarded instead of becoming a burst of catch-up simulation frames, while transport/application realtime never moves backward.

## Client Render Pause

Quake II rebuilds `cl.refdef` only when unpaused and submits the cached refdef while paused. OpenRealm follows that lifecycle, with one additional requirement: WC3 MDX particle emitters run from `R_DrawEntities()` during `RenderFrame`, not while the client constructs its entity list. A cached entity list alone therefore still re-enters emitters.

Paused world submission must set `viewDef.deltaTime = 0`. Otherwise an emitter held on an active animation frame repeatedly accumulates and drains particles in `R_EmitParticles`; Instruments then reports the main thread continuously in `MDLX_RenderHeadEmitter -> R_EmitParticles -> R_SpawnParticle`, and the application appears hung at full CPU. Keep the raw client clock current during pause so resume receives only the next real frame delta rather than the whole paused duration.

## Warcraft Pause Sources

`games/warcraft-3/game/g_main.c` currently combines two sources:

| Source | State | Behavior |
|---|---|---|
| JASS `PauseGame(flag)` | `level.script_paused` | Global authoritative pause requested explicitly by map script. |
| Quest dialog | `level.quest_paused` | Pauses only when exactly one game client is connected and that client's Quest dialog is open. |

The final engine state is:

```text
script_paused || quest_paused
```

Quest pause is intentionally single-client-only. A local campaign Quest dialog may freeze its simulation, but one player's F9/Quest UI must not globally stop a multi-client match.

Each WC3 client tracks `quest_dialog_open`. Disconnect clears that ownership and recomputes pause state, preventing an abandoned modal from leaving the server paused.

## Modal Window Input

`CL_WindowModalActive()` is true when the client window list contains a modal window. `CL_WindowMouseEvent()` dispatches only
to the topmost modal and returns consumed even for clicks outside its bounds; `CL_WindowKeyEvent()` likewise scopes hotkeys to
that modal. Windows are retained from `svc_window` packets and removed locally by the client-owned close actions.

While a modal is active:

- mouse clicks are dispatched only to the active modal window;
- window hotkeys are dispatched only inside the active modal window;
- clicks in transparent space outside a modal are still consumed by the window manager;
- selection, Smart orders, minimap recenter/drag, control groups, middle-button pan, arrow-key scrolling, and edge scrolling are suppressed;
- any world drag already in progress is cancelled.

Persistent `svc_layout` layers remain a separate HUD path. New transient gameplay dialogs should use `svc_window` rather than
adding another modal layout layer.

## Time Semantics

Global pause freezes anything that depends on server simulation advancement because `ge->RunFrame()` is not called and `sv.time` is unchanged. In particular, WC3 systems using `gi.GetTime()` see a frozen clock, including `TriggerSleepAction` scheduling and spell/camera/message deadlines.

`PauseTimer` / `ResumeTimer` remain separate JASS timer work; their native bodies are still stubs in `games/warcraft-3/game/api/api_misc.h`. Global pause does not make those object-level natives implemented.

`SuspendTimeOfDay` is a separate implemented clock control: it freezes only ordinary Warcraft daily-cycle progression and does not call the global server pause gate. `SetTimeOfDayScale` remains incomplete. See [Time Of Day](time-of-day.md).

## Known Pitfalls

- **Do not stop `SV_Frame` wholesale.** The server must continue `SV_ReadPackets()` so the close command can arrive and must continue sending traffic so clients do not time out.
- **Do not let paused wall-clock time become resume work.** Rebase `sv.next_frame_msec` when leaving pause; keep `svs.realtime` monotonic for application/network timing.
- **Do not use only frame hit-testing for a modal.** Transparent modal areas must still block world input, and camera edge/arrow scrolling has no pointer hit-test at all.
- **Do not globally pause multiplayer because one Quest dialog opened.** Quest ownership is local presentation; only the single-client session policy maps it to a simulation pause.
- **Do not equate modal input with simulation pause.** `UI_WINDOW_NO_PAUSE` exists for dialogs such as Allies that must block world input while time continues.
- **Do not make mandatory result/decision windows Escape-dismissable.** Use `UI_WINDOW_NO_ESCAPE` when explicit button commands own the continuation.
- **Do not conflate `PauseUnit` with global pause.** `PauseUnit` is WC3 entity state and intentionally has narrower gameplay semantics.
- **Do not keep rebuilding or advancing the world refdef while paused.** Cached MDX entities still execute model render code, so submit them with zero `deltaTime` or particle emitters can saturate the main thread.

## Verification

Build and test:

1. Start a single-player campaign map and begin movement/combat/construction.
2. Open Quests. Verify unit/combat/construction state and `sv.time` remain unchanged while rendering/UI stay responsive.
3. Leave Quests open for more than 10 seconds. Verify no timeout/disconnect occurs.
4. While Quests is open, verify arrow keys, edge scroll, middle-button pan, minimap drag, selection, Smart orders, and control groups do nothing.
5. Click Done. Verify the dialog closes, the next simulation step resumes normally, and there is no fast-forward burst.
6. Reopen/close Quests repeatedly to verify pause ownership does not become stuck.
7. In a multi-client session, open one player's Quest dialog and verify the authoritative simulation continues.
8. Exercise a map calling `PauseGame(true)` and release the pause through a client/server action that calls `PauseGame(false)`; verify the same scheduler and timeout behavior.

## See Also

- [UI Flow](architecture/ui-flow.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [Triggered Dialogue](triggered-dialogue.md)
- [Runtime Architecture](../../architecture/runtime.md)
