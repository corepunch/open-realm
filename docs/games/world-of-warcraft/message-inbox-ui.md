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

## Welcome window and gameplay input

`Wow_ClientBegin` sends the welcome tutorial through `UI_WriteWelcomeWindow` as a
unique client window. Keep this informational window non-modal: the shared
`CL_GameplayInputReady` gate blocks keyboard movement and right-mouse camera
look whenever a modal window is open. The welcome window's own bounds still
capture mouse actions through `CL_WindowMouseOver`, so clicks on its Okay button
stay with the window while WASD and camera input remain available.

The server-authored welcome text and button are built in `game/g_ui.c`; the
`WelcomeFrame.xml` test fixture is not the runtime source for this window.
