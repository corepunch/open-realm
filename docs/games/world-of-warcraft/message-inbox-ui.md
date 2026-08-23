# Client-rendered message inbox UI

## Decision

Quest completion messages and similar bottom-of-screen notifications should use a
server-owned data model with client-owned presentation. The server decides which
message exists, its stable ID, recipient, text/content references, and read state.
The client receives a bounded inbox payload over `svc_gamecmd`, stores it in the
WoW UI module, and renders notification icons and message windows through the
existing Lua/FDF UI path.

The server must not send absolute window coordinates or a complete UI frame tree
for this feature. Positions, scaling, input hit-testing, stacking, and window
decoration belong to the client and can change without changing the gameplay
protocol.

## Why this matches the engine

- `svc_gamecmd` is already a reliable server-to-client channel for typed game
  payloads and is separate from snapshot/network-contract structs.
- The current WoW HUD is drawn by the client UI library, while server code still
  emits `svc_layout` frames for quest dialogs and the HUD. The inbox is a useful
  migration target away from server-positioned frames.
- Quake 3's reliable server commands are the closest lifecycle analogue: durable
  UI events must not depend on a particular snapshot arriving, while movement and
  continuously changing state remain snapshot-like.
- Inventory should use the same split: server-authoritative item IDs/counts and
  permissions; client-owned bag/equipment window layout and drag/drop visuals.
  Client actions remain requests validated by the server.

## Proposed payload

The first protocol version is a bounded full snapshot, not a stream of individual
notifications. A snapshot is idempotent, easy to resend after reconnect/map load,
and avoids client/server divergence when several quest rewards arrive together.

Each message record should contain only gameplay data needed by the client:

| Field | Owner | Purpose |
| --- | --- | --- |
| `message_id` | server | Stable per-character ID used by UI actions |
| `kind` | server | Quest reward, system, mail, etc. |
| `flags` | server | Unread/urgent/has-choice state |
| `sender_key` | server/data | Localization or display identity |
| `title_key` | server/data | Localizable title |
| `body_key` + parameters | server/data | Localizable body/content |
| `quest_id` / `item_id` | server | Optional semantic links |
| `created_time` | server | Ordering and display metadata |

Prefer bounded IDs and data keys over arbitrary client-executable text. If the
initial implementation needs literal text for tests, keep it length-delimited and
validate its maximum length at the protocol boundary.

## Interaction contract

1. Server creates or updates a record after the authoritative quest/reward event.
2. Server sends the current inbox snapshot through `svc_gamecmd`.
3. Client stores the records and draws one notification icon per unread record,
   capped by a client layout limit.
4. Clicking an icon opens the client-positioned message window.
5. Client sends a semantic command containing only the message ID.
6. Server validates ownership/state, marks the record read if appropriate, and
   sends a refreshed snapshot.

Opening a window is a presentation action; accepting a reward, claiming an item,
or acknowledging a quest consequence remains server-authoritative.

## Implementation plan

### Phase 1 — transport and data model

- Add a generic game-command callback from the client parser to `uiExport_t` so
  game-specific payloads do not become engine-specific branches.
- Add a WoW inbox payload schema and bounded parser in the WoW UI module.
- Add server-side per-client message records and a helper that sends a full
  snapshot through the existing game-command transport.
- Add tests for round-trip parsing, truncation/rejection, duplicate IDs, and
  idempotent replacement.

### Phase 2 — first visible slice

- Create one message when a quest is rewarded.
- Expose `ow3.messages()` to the existing Lua HUD.
- Render unread notification squares at a client-owned bottom-screen anchor and
  open a client-owned message window on click.
- Add tests for reward → inbox payload and read command validation.

### Phase 3 — reusable client windows

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

- generic `svc_gamecmd` dispatch into `uiExport_t.GameCommand`;
- bounded version-1 `wow_inbox` snapshots with up to eight records;
- quest reward → unread inbox record and client-begin snapshot delivery;
- server-validated `message_read <id>` handling;
- client-owned active-game notification strip and message window; unread
  records use `TutorialFrameAlert` with the 34x42 size, bottom-55 anchor, and
  texture crop from `TutorialFrame.xml`;
- regression coverage for reward delivery and read-state changes.

## Tutorial tips

The welcome panel is classic tutorial ID 42, not a `WelcomeFrame`. The server
sends the semantic `TutorialFrame` window request at client begin. The WoW UI
loads `GlobalStrings.lua` and resolves `TUTORIAL_TITLE42` and `TUTORIAL42`, then
draws the measurements and assets authored by `TutorialFrame.xml`: 230px width,
`TutorialFrameBackground`, tooltip border, check box, and 76x21 Okay button.
The reusable client path retains `tutorial_id`, title, and body, so later
tutorial triggers can use the same presentation rather than adding one-off HUD
frames.

At client begin the server sends versioned `wow_tutorial` triggers for tutorial
IDs 1 (`Questgivers`) and 2 (`Movement`), alongside the welcome-window request.
This mirrors `TutorialFrame_NewTutorial`, which receives numeric
`TUTORIAL_TRIGGER` events in the original Lua. Each appears above the action bar as the 34x42
`TutorialFrameAlert` crop authored by `TutorialFrame.xml`; clicking an alert
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
