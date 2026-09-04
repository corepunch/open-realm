# Client Windows

## Ownership

`client/cl_window.c` owns transient server-authored gameplay windows. Persistent singleton HUD planes remain `svc_layout`
layers. Do not add a `UILAYOUTLAYER` member for an inventory, journal, quest description, or other spawnable window.

The server owns a window's instance ID, opaque game-local class ID, flags, frame tree, and close lifecycle. The client owns
z-order, keyboard focus, pointer capture, and the local drag offset. Updating an existing instance preserves its drag offset.
Opening a `UI_WINDOW_UNIQUE` class replaces and focuses its existing instance.

After opening a pause-owning modal `svc_window`, the client compares pause-owner presence with its last reported pause state.
The first modal without `UI_WINDOW_NO_PAUSE` sends `pause 1`; closing the last pause-owning modal sends `pause 0`. Opening or
closing nested modals sends nothing while a pause owner remains. The game keeps this client request as an independent pause
owner, so it cannot clear a script-owned `PauseGame(true)`. `UI_WINDOW_NO_PAUSE` keeps modal input capture while opting that
window out of this pause handshake; the Warcraft III Allies dialog uses this split.

Windows form a doubly linked list. The head is backmost and the tail is frontmost. Drawing walks head to tail; pointer hit
testing walks tail to head. Left-clicking a window unlinks it and appends it at the tail, making draw order and keyboard focus
change together. Closing the focused window focuses the new tail.

`SCR_DrawLayout()` draws persistent layout layers first and then calls `CL_WindowDraw()`, placing transient windows above the
HUD. Parsing and retaining `svc_window` does not make a window visible by itself; the screen layout pass owns submission.

The legacy `menu.dll` is a menu module, following the Quake II `key_dest` split: `cl_scrn.c` refreshes it only for `key_menu`,
and `cl_input.c` forwards its keyboard/mouse events only for `key_menu`. The active-game pass draws server-authored layout and
windows through `SCR_DrawLayout()`/`CL_WindowDraw()`; it must not call a UI-DLL game overlay or send UI-DLL input.

WoW incoming messages use `FT_MESSAGE_QUEUE` on `LAYER_MESSAGE`. The server emits one compact record per unread message and,
when selected, one additional record carrying the server-owned popup text. `cl_scrn.c` draws both the unread icon pool and the
popup; clicks send `message_open <id>` or `message_close` back to the server, which updates the authoritative layer.

`UI_WINDOW_MODAL`, `UI_WINDOW_NO_PAUSE`, and `UI_WINDOW_UNIQUE` are independent. Modal means the topmost modal window consumes
input outside its bounds. `NO_PAUSE` means that modal does not acquire the client-owned simulation pause. Unique means only one
instance of that class may exist. Inventory and quest-detail windows can be unique without being modal; confirmation-style
windows are normally modal pause owners, while the WC3 Allies dialog is modal plus `NO_PAUSE`.

## Wire Format

```text
svc_window
  byte open
  long instance_id

UI_WINDOW_OPEN:
  long class_id
  long flags
  window frame records
  long 0, short 0
  long text_size
  byte text[text_size]

```

Window frame records retain the normal one-byte type-specific payload size. The `text`, `tooltip`, and `onclick` fields differ:
they are 32-bit offsets into the trailing text arena instead of inline strings. Offset zero means no string; byte zero of every
arena is therefore NUL. The client validates every nonzero offset and requires a terminating NUL within `text_size` before
retaining the packet.

The arena is bounded by `MAX_MSGLEN`, not the 255-byte typed-payload field. This allows quest descriptions, message histories,
and other long text blocks without inflating frame-specific structs. `SCR_ClearWindow` resolves offsets directly into the
retained arena; it does not duplicate strings.

## Input

- Mouse-down on a window raises it and assigns keyboard focus.
- Mouse-down on non-command background starts a drag when `UI_WINDOW_MOVABLE` is set.
- Drag capture continues through motion and mouse-up outside the window.
- Keyboard hotkeys are searched only in the focused window, or the topmost modal window.
- Clicking outside all non-modal windows clears focus.
- A modal window blocks world hit testing, control groups, bindings, minimap actions, and manual camera movement.
- Key-up still reaches gameplay `+command` releases so opening or focusing a window cannot leave an input held.
- `close_window_command <command>` forwards the suffix to the server and then closes the owning window; use it for transactional
  Accept/Cancel buttons that must commit or discard server-owned draft state before dismissal.
- Scrollable TextArea state is client-owned. Because a retained `svc_window` packet is reparsed for each draw/hit test,
  `client/cl_window.c` stores scroll fractions by frame number and reapplies them after each parse. Mouse wheel input, scrollbar
  arrows/track, and thumb dragging update that retained value rather than mutating only the temporary parsed frame.

### Client-owned button actions

Most authored window `onclick` strings are sent back to the game server. Four explicit action tokens are consumed locally by
`client/cl_window.c` instead:

- `UI_WINDOW_CLOSE_ACTION` (`close_window`) closes the owning window;
- `UI_WINDOW_CLOSE_NOTIFY_ACTION` (`close_window_notify`) closes it and therefore participates in modal-list pause synchronization;
- `UI_WINDOW_DISCONNECT_ACTION` (`disconnect_game`) leaves the current game and returns to the front-end;
- `UI_WINDOW_QUIT_ACTION` (`quit_application`) queues normal application shutdown.

Disconnect and quit are intended for explicit local activation in server-authored menus. They are never executed merely because a
window packet is received. Submenu/navigation actions remain ordinary server commands.

## Legacy UI Windows

`svc_ui_window` retains the older WoW `ui.ShowWindow` named-XML toggle. It is migration debt and does not participate in the
generic linked list, focus, dragging, or modal ownership. New gameplay windows must use `svc_window`.

## Validation

```sh
make openwarcraft3
make test
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +set skip_cutscene 1 \
  +map 'Maps/Campaign/Human02.w3m' +exec /tmp/openwarcraft3-quest-window.cfg +com_frame_limit 175
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +set skip_cutscene 1 \
  +map 'Maps/Campaign/Human02.w3m' +exec /tmp/openwarcraft3-log-window.cfg +com_frame_limit 175
```

Each temporary config contains 150 `wait` commands followed by `cmd quests` or `cmd log`, then `screenshot 5`. This delay lets
Human02 reach `ca_active` before the command is forwarded to the loopback server. A main-menu launch does not test this protocol:
there is no connected game server to serialize or parse `svc_window`.

The connected Quest run found that authored FDF frames must use `PF_UIWINDOWFRAME` while `ui_window_writing` is true. Sending
them as `PF_UIFRAME` made the inline-string codec dereference the first arena offset (`0x1`). Proxy and authored frames must both
select the offset-aware codec. `wc3_game.hud_authored_window_frame_uses_offset_codec` guards this boundary.

The standalone net tests cover text offsets, a text arena above 255 bytes, screen-pass drawing, unique-class replacement,
linked-list raise order, keyboard focus, and malformed packets without a frame terminator.

## See Also

- [Warcraft III Allies Menu](../games/warcraft-3/allies-menu.md)
