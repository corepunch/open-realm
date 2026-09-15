# Timer Dialogs And Mission Countdowns

OpenRealm implements ordinary Warcraft III mission countdowns as a JASS timer plus the stock `TimerDialog.fdf` presentation. Campaign code such as a Human05-style survival countdown does not need a mission-specific HUD.

## Runtime Ownership

The simulation timer remains authoritative:

```text
CreateTimer / TimerStart
        |
        v
GTIMER.duration / remaining
        |
        +--> expiration callback and timer-expire events
        |
        v
TIMERDIALOG handle
        |
        v
UI\\FrameDef\\UI\\TimerDialog.fdf
```

A `TIMERDIALOG` stores only presentation state and a pointer to the existing `GTIMER`. Destroying or hiding a dialog never destroys, pauses, resumes, or reschedules the associated timer.

The fixed `level.timer_dialogs[MAX_TIMERDIALOGS]` registry gives JASS light handles stable map-lifetime addresses. Each slot stores the timer association, title, optional title/time colours, and a client visibility mask.

## Implemented Natives

The ordinary campaign path is implemented:

- `CreateTimerDialog`
- `DestroyTimerDialog`
- `TimerDialogSetTitle`
- `TimerDialogSetTitleColor`
- `TimerDialogSetTimeColor`
- `TimerDialogDisplay`
- `IsTimerDialogDisplayed`

`TimerDialogDisplay` follows the existing `currentplayer` convention. Inside a local-player branch it changes only that client's visibility bit; outside a local branch it applies to all client slots. The visibility mask is presentation state and does not alter the gameplay timer.

Titles pass through `G_LevelString()` before being copied into dialog state, so ordinary `TRIGSTR_` mission strings use the existing map-string resolution path.

RGBA arguments are clamped to byte range before they reach the FDF string frames.

## HUD Presentation

`UI_LoadHudTimerDialogs()` binds the stock generated `TimerDialog_t` tree from:

```text
UI\\FrameDef\\UI\\TimerDialog.fdf
```

The stock root is re-anchored at the top-right of a widescreen-aware HUD anchor. Its top edge uses the same `0.035` vertical offset as the first Hero shortcut, with the Hero controls' `0.006` edge inset mirrored to the actual right edge of the screen. The TimerDialog's authored size and child layout remain unchanged.

The runtime updates:

- `TimerDialogTitle`
- `TimerDialogValue`

through the dedicated `LAYER_TIMERDIALOG` server-authored layout layer. The new layer is appended after existing WC3 layers so older numeric layer assignments do not move.

A dialog starts hidden. Once displayed, `G_UpdateTimerDialogs()` checks connected clients after JASS timer/event work each server frame. It resends the timer layer only when:

- visibility/dialog selection changed;
- title or colour state was dirtied; or
- the displayed whole second changed.

This avoids rebuilding unrelated HUD layers every frame.

The displayed value is zero-padded `MM:SS`:

```text
30:00
09:07
01:04
00:09
00:00
```

Minutes are not capped at two digits. The formatter uses the timer's remaining millisecond countdown; the timer callback remains governed solely by `G_RunTimers()`.

`UI_ShowInterface(false)` already hides every normal HUD layer except `LAYER_CINEMATIC`, so the timer dialog follows the existing cinematic/interface visibility policy without special-case code.

## Reconnect And Save/Load

`G_ClientBegin()` writes the current timer-dialog layer so a connecting/reconnecting client receives the authoritative presentation state.

Save format version 23 persists:

- all fixed timer-dialog slots and `inuse` lifecycle state;
- timer association through `F_TIMER` relocation;
- title and colour state;
- client visibility mask; and
- JASS `timerdialog` handle identity through a stable slot-index codec.

The `svc_layout` payload itself is not serialized. After `ReadGame()`, timer-dialog client caches are invalidated and the restored presentation is republished on the next frame.

## Known Limits

`TimerDialogSetSpeed` remains intentionally unsupported. It consumes its arguments but does not modify the gameplay timer or presentation until retail display-rate semantics are pinned down confidently.

`TimerDialogSetRealTimeRemaining` remains unregistered/unimplemented for the same reason. Ordinary campaign countdowns do not require it.

The state model supports multiple timer-dialog handles, but the current HUD renderer presents the lowest-slot visible dialog for each client. Exact retail simultaneous-dialog stacking/packing has not been established, so no speculative layout policy is implemented yet.

Title and colour state are shared authoritative dialog state. Only display visibility currently has per-client local-state semantics.

## Regression Coverage

The WC3 engine tests cover:

- non-null timer-dialog allocation from JASS;
- local visibility and `IsTimerDialogDisplayed`;
- title and clamped/independent RGBA state;
- destroying a dialog without destroying its timer;
- zero-padded countdown formatting; and
- save/load restoration of timer-dialog state and JASS alias identity.

See [JASS Native Coverage](jass-native-coverage.md), [Save/Load](save-load.md), and the engine-wide [UI System](../../architecture/ui-system.md).
