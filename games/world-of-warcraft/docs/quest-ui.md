# Quest UI

WoW game mode keeps in-game UI server-authored. The client-side reference
assets are loaded from the installed client data:

- `Interface\\FrameXML\\QuestFrame.xml` — classic panel dimensions, anchors,
  child positions, and button layout.
- `Interface\\FrameXML\\QuestFrame.lua` — quest greeting/detail/progress/
  reward state transitions and button event names.
- `Interface\\QuestFrame\\UI-QuestGreeting-*` — the four 256/128-pixel panel
  art pieces used by the server layout.

The live implementation is `game/g_ui.c`. It sends `svc_layout` frames on
`LAYER_QUESTDIALOG`; this is layer 8 in `common/shared.h`. The client stores
and draws that layer through the normal layout renderer, so the WoW UI module
does not run quest Lua while `game_mode` is active.

## Positioning

The server layout uses the WoW normalized 1.0×1.0 UI canvas. `g_ui.c` keeps a
1024×768 reference canvas (`PX`, `PY`, `PW`, and `PH` helpers). The current
quest panel is 384×512, positioned at `(24, 104)` in that canvas. These values
correspond to the classic QuestFrame dimensions while leaving the server HUD
target frame visible.

## Server flow

- The HUD quest icon is a server-generated clickable region sending `quest`.
- Selecting a spawned quest giver and issuing `quest` opens that giver's
  server-data quest. `quest <id>` can open a known quest directly.
- `quest_close` hides the layer; `quest_accept <id>` currently closes the
  dialog and is the hook for persistent quest state and rewards.
- The server sends an empty `LAYER_QUESTDIALOG` layout whenever the dialog is
  closed, preventing stale client frames.

Quest placement and titles are imported under
`serverdata/world-of-warcraft/`; quest progress, rewards, and full text are
the next server-data additions. WoWee is useful as a client/reference project
for the broader quest protocol and FrameXML behavior, but this codebase keeps
the in-game presentation on the server layout channel.
