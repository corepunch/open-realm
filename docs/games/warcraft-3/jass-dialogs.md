# JASS choice dialogs

The Warcraft game module owns `DialogCreate`, `DialogDestroy`, `DialogClear`,
`DialogSetMessage`, `DialogAddButton`, `DialogAddQuitButton`, `DialogDisplay`,
`TriggerRegisterDialogEvent`, `TriggerRegisterDialogButtonEvent`,
`GetClickedButton`, and `GetClickedDialog`.

## Ownership and input

`level.dialogs` and `level.dialog_buttons` are bounded pools (64 dialogs, 256
buttons). `DialogClear` and `DialogDestroy` release slots for reuse, and each
reuse advances a generation stored in the ID's high bits
(`(generation << 16) | (slot + 1)`). A late click, a stale registration, or a
saved ID for a released occupant never matches the slot's new occupant.
Exhaustion is refused and logged to stderr. The server
retains the message, labels, hotkeys, visibility, and ownership; the universal
client receives an ordinary server-authored `svc_window` and returns
`jassdialog <dialog-id> <button-id>`. The game validates both IDs, parentage,
client identity, and player-specific visibility, then consumes that visibility
before emitting an event. Duplicated network commands are ignored.

`UI_WINDOW_CLOSE` is a generic server-to-client `svc_window` operation carrying
only the window ID. It is not a Warcraft-specific UI protocol. It lets
`DialogDisplay(false)`, `DialogClear`, and `DialogDestroy` release modal focus
without synthesizing an answer. The client's `close_window_command` handles a
selected button's local closure without changing simulation state.

The generic interactive window uses `UI_WINDOW_MODAL | UI_WINDOW_UNIQUE |
UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE`; it captures gameplay input without
inventing a simulation pause. Text is resolved through `G_LevelString` and the
existing FDF UI layer.

## Events and save/load

The event registration records a dialog ID and optional button ID. A click
queues the matching `EVENT_DIALOG_CLICK` and/or `EVENT_DIALOG_BUTTON_CLICK`
registration through the normal `GAMEEVENT` pipeline. `JASSCONTEXT` stores
both IDs and the clicking player's `playerState`, including across coroutine
suspension. The response survives game saves via the level event serializer,
JASS snapshots, and stable dialog/button handle indexes. The game save version
is **67**, and the JASS snapshot version is **7**; earlier layouts are rejected.

A choice is one-shot per `DialogDisplay` call. Re-display is allowed and does not
recreate the JASS handles. A new dialog shown to the same player replaces the
previous active choice and clears its authoritative visible bit.

## Deliberate remaining compatibility work

* `DialogAddQuitButton` creates an ordinary selectable button carrying the
  quit and score-screen metadata. The retail-specific quit/end-game transition
  and score-screen policy are not implemented here.
* Button hotkeys are stored, but the generic `svc_window` keyboard shortcut
  routing is not implemented. Mouse input is supported.
* UI emits at most 12 buttons in a window; omitted choices are reported to
  stderr. Unusually large custom-map dialogs need scrolling/pagination and
  retail scaling research.
* ScriptDialog and ScriptDialogButton use generated bindings to stock FDF.
  The retail FDF has no variable-height choice rows, so row anchors and dialog
  height are generated from the runtime button count. Final visual alignment,
  actual clickable rendering, and exact retail dialog layout require in-game
  verification at 640×480 and widescreen resolutions.
* Save restores dialog identities and visibility. `ClientBegin` on the
  post-load reconnect republishes the active choice once; manual
  save/reconnect testing is still required.
* Nested simultaneous dialogs, priority and per-button hotkey collision rules
  need further retail verification.

The NightElfX06 fork requires both the global dialog event and specific-button
event pathways. It must not use a map-specific event or client UI hook.

### Regression tests

`wc3_dialog.*` covers creation, distinct handles, clear/destroy lifecycle,
JASS native calls, a choice event, correct `GetClickedButton` /
`GetClickedDialog` / `GetTriggerPlayer` context, registration selection, and
duplicate click rejection. It also verifies player-number identity when a
client slot differs from its Warcraft player number, verifies clear hides the
matching client's window, and checks that repeated serialization reclaims
temporary FDF frames. Further cases cover slot reuse across 100 clear/destroy
rounds, stale-ID rejection after reuse, malformed or foreign `jassdialog`
commands, clicked-dialog context across a save taken while the action sleeps,
and a single republish after load. The disabled-label
serialization path and save-version rejection have focused regressions.
`make test` passed with 46,715 assertions after the audit fixes. Retail visual
alignment and campaign integration still require user-side game verification.
