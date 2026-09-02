# Warcraft III Pause And Modal UI

## Contract

OpenRealm separates application/network progress from deterministic Warcraft III simulation time.

- `server/sv_main.c` owns the authoritative simulation pause gate. `SV_SetPaused(true)` stops `SV_RunGameFrame()`; it does **not** stop `SV_ReadPackets()` or client transport.
- `games/warcraft-3/game/` owns Warcraft pause policy. `PauseGame` and single-client Quest-dialog ownership are combined before calling the generic `game_import.SetPaused` hook.
- `client/cl_scrn.c` owns server-authored modal layout input. `LAYER_QUESTDIALOG` and `LAYER_GAME_RESULT` are modal layers: when either is visible, lower layout buttons and the 3D world do not receive pointer/hotkey interaction.
- `client/cl_input_w3.c` owns WC3 camera/selection gestures. A modal cancels active pan, minimap drag, selection drag, hover, arrow scrolling, and edge scrolling.

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

While paused, `sv.time` and `sv.framenum` remain frozen. At `FRAMETIME` cadence the server still sends a snapshot packet with the frozen frame/time. This traffic is deliberate: `client/cl_main.c` disconnects after `CL_TIMEOUT_MSEC` (10 seconds) without a server packet, so returning from `SV_Frame` before transport would turn a long pause into `Connection to host timed out.`

On resume, `SV_SetPaused(false)` rebases only `sv.next_frame_msec` to the current monotonic `svs.realtime`. Wall-clock time spent paused is therefore discarded instead of becoming a burst of catch-up simulation frames, while transport/application realtime never moves backward.

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

## Modal Layout Input

`SCR_LayoutModalActive()` is true when a visible `LAYER_GAME_RESULT` or `LAYER_QUESTDIALOG` exists. These layers are server-authored `svc_layout` data, so they remain interactive even when normal gameplay input is rejected.

A terminator-only `svc_layout` payload is a layer clear. `CL_ParseLayout()` leaves that layer slot `NULL` instead of retaining an empty allocation; modal ownership therefore ends in the same packet that hides the dialog.

While a modal is active:

- `CL_GameplayInputReady()` is false for WC3 world input;
- mouse clicks are dispatched only inside the active modal layout layer;
- layout hotkeys are dispatched only inside the active modal layer;
- `SCR_LayoutHitTest()` treats the whole screen as UI-owned, including transparent areas around the dialog;
- selection, Smart orders, minimap recenter/drag, control groups, middle-button pan, arrow-key scrolling, and edge scrolling are suppressed;
- any world drag already in progress is cancelled.

This is separate from the client-side glue/menu modal system in `games/warcraft-3/ui/ui_render.c`. In-game Quest/Game Result frames arrive through `svc_layout`, so their modal ownership belongs in the generic layout client path.

## Time Semantics

Global pause freezes anything that depends on server simulation advancement because `ge->RunFrame()` is not called and `sv.time` is unchanged. In particular, WC3 systems using `gi.GetTime()` see a frozen clock, including `TriggerSleepAction` scheduling and spell/camera/message deadlines.

`PauseTimer` / `ResumeTimer` remain separate JASS timer work; their native bodies are still stubs in `games/warcraft-3/game/api/api_misc.h`. Global pause does not make those object-level natives implemented.

`SuspendTimeOfDay` and `SetTimeOfDayScale` are likewise separate incomplete natives. Do not couple them to the global server pause gate.

## Known Pitfalls

- **Do not stop `SV_Frame` wholesale.** The server must continue `SV_ReadPackets()` so the close command can arrive and must continue sending traffic so clients do not time out.
- **Do not let paused wall-clock time become resume work.** Rebase `sv.next_frame_msec` when leaving pause; keep `svs.realtime` monotonic for application/network timing.
- **Do not use only frame hit-testing for a modal.** Transparent modal areas must still block world input, and camera edge/arrow scrolling has no pointer hit-test at all.
- **Do not globally pause multiplayer because one Quest dialog opened.** Quest ownership is local presentation; only the single-client session policy maps it to a simulation pause.
- **Do not conflate `PauseUnit` with global pause.** `PauseUnit` is WC3 entity state and intentionally has narrower gameplay semantics.

## Verification

Build and test externally (this change was prepared without compiling locally):

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
