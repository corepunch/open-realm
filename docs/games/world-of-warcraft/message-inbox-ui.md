# Server-authored message inbox UI

## Decision

Quest completion messages and similar notifications use a server-owned data model
and a server-authored UI control. The server decides which message exists, its
stable ID, recipient, text, and read state. It sends a bounded `FT_MESSAGE_QUEUE`
frame on `LAYER_MESSAGE`; `cl_scrn` draws the unread notification controls and the
selected message window.

The server must not send absolute window coordinates or a complete UI frame tree
for this feature. Positions, scaling, input hit-testing, stacking, and window
decoration belong to the client and can change without changing the gameplay
protocol.

## Why this matches the engine

- `FT_MESSAGE_QUEUE` follows the existing `FT_BUILDQUEUE` special-control pattern:
  the server supplies bounded data and the client screen renderer owns pixels.
- The message control is part of the same `svc_layout` stream as the server-owned
  WoW HUD and quest dialog, so it is redrawn with the authoritative UI state.
- Inventory should use the same split: server-authoritative item IDs/counts and
  permissions; client-owned bag/equipment window layout and drag/drop visuals.
  Client actions remain requests validated by the server.

## Payload

Each visible message is one bounded `FT_MESSAGE_QUEUE` frame. The server emits all
unread records and the currently open record, making the layer idempotent and easy
to resend after reconnect, map load, or other HUD refreshes.

Each message record should contain only gameplay data needed by the client:

| Field | Owner | Purpose |
| --- | --- | --- |
| `message_id` | server | Stable per-character ID used by UI actions |
| `image` | server/data | Notification art resource |
| `title_font` / `body_font` | server/data | Text resources |
| `flags` | server | Unread/open state |
| `text` / `tooltip` | server | Title and body strings |

Prefer bounded IDs and data keys over arbitrary client-executable text. If the
initial implementation needs literal text for tests, keep it length-delimited and
validate its maximum length at the protocol boundary.

## Interaction contract

1. Server creates or updates a record after the authoritative quest/reward event.
2. Server emits the current `LAYER_MESSAGE` controls through `svc_layout`.
3. `cl_scrn` draws one notification icon per unread record and the open panel.
4. Clicking an icon sends `message_open <id>`; closing sends `message_close`.
5. Server validates the ID, owns open/read state, and emits refreshed controls.

Opening a window is a presentation action; accepting a reward, claiming an item,
or acknowledging a quest consequence remains server-authoritative.

## Implementation plan

### Phase 1 — first visible slice

- Create one message when a quest is rewarded.
- Emit it as an `FT_MESSAGE_QUEUE` frame on `LAYER_MESSAGE`.
- Render unread notification icons and the selected message panel in `cl_scrn`.
- Validate `message_open <id>` and `message_close` on the server.

### Phase 2 — reusable client windows

- Introduce a small client-side window registry with IDs, anchors, modal state,
  and z-order. Keep layout in Lua/FDF rather than in game C.
- Move quest log/dialog and inventory presentation toward this registry while
  retaining server-authoritative command validation.
- Add keyboard escape, focus, and mouse capture rules once more than one window
  can be open.

### Phase 4 — persistence and richer content

- Persist message records/read state with the character/session model.
- Add item/quest links and localization parameters.
- Replace full snapshots with revisioned deltas only after reconnect and loss
  behavior is covered by tests.

## Constraints

- Do not widen `entityState_t` or `playerState_t` for inbox state.
- Do not let client commands claim rewards or mutate inventory without server
  validation.
- Do not use unbounded strings or arbitrary script supplied by the server.
- Keep the initial notification count and payload size bounded; log rejected
  records with `UIWow:` or `WoW:` diagnostics rather than silently dropping them.

## Current implementation status

Implemented in the current tree:

- `FT_MESSAGE_QUEUE` controls on `LAYER_MESSAGE`;
- quest reward → unread server message record;
- `cl_scrn` notification icon and message panel drawing;
- server-owned open/read state and `message_open`/`message_close` commands;
- regression coverage for reward delivery and open/close state changes.

## Tutorial tips

The welcome panel is classic tutorial ID 42, not a `WelcomeFrame`. The server sends the semantic `TutorialFrame` window request at
client begin. The WoW UI loads `GlobalStrings.lua`, resolves `TUTORIAL_TITLE42` and `TUTORIAL42`, then binds them into Blizzard's
`Interface/FrameXML/TutorialFrame.xml`. Its zero-height body is measured at runtime and the frame follows native
`TutorialFrameText:GetHeight() + 62` sizing, so localized text expands the backdrop.
The reusable client path retains `tutorial_id`, title, and body, so later
tutorial triggers can use the same presentation rather than adding one-off HUD
frames.

At client begin the server sends versioned `wow_tutorial` triggers for tutorial
IDs 1 (`Questgivers`) and 2 (`Movement`), alongside the welcome-window request.
This mirrors `TutorialFrame_NewTutorial`, which receives numeric `TUTORIAL_TRIGGER` events in the original Lua. Each appears above
the action bar using the native alert dimensions and runtime sibling stride; clicking an alert
removes it from the bounded client queue and opens the same localized tutorial
panel for that ID. Inbox notifications share the strip geometry but remain
separate server-authored records.

`ui_show_tips` is the `Display Tips` cvar (default `1`). The client suppresses
incoming tutorial-window requests when it is `0`; the check box changes it
through the UI cvar import. Missing localized tutorial keys emit a `UIWow:`
diagnostic instead of displaying an empty panel.

The Okay button follows the XML button press/release contract: left mouse down
swaps the art to `UI-Panel-Button-Down` and arms `tutorial_okay_pressed`, and
left mouse up over the button closes the panel. Releasing off the button clears
the pressed state without closing.

The remaining work is persistence, localization keys instead of the initial
literal quest text, close/focus behavior for multiple windows, and moving the
existing server-positioned quest/inventory panels onto the same client window
registry.
