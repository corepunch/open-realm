# Client Windows

## Ownership

`client/cl_window.c` owns transient server-authored gameplay windows. Persistent singleton HUD planes remain `svc_layout`
layers. Do not add a `UILAYOUTLAYER` member for an inventory, journal, quest description, or other spawnable window.

The server owns a window's instance ID, opaque game-local class ID, flags, frame tree, and close lifecycle. The client owns
z-order, keyboard focus, pointer capture, and the local drag offset. Updating an existing instance preserves its drag offset.
Opening a `UI_WINDOW_UNIQUE` class replaces and focuses its existing instance.

After opening a modal `svc_window`, the client compares modal-list presence with its last reported pause state. The first modal
sends `pause 1`; closing the last modal sends `pause 0`. Opening or closing nested modals sends nothing while the list remains
modal. The game keeps this client request as an independent pause owner, so it cannot clear a script-owned `PauseGame(true)`.

Windows form a doubly linked list. The head is backmost and the tail is frontmost. Drawing walks head to tail; pointer hit
testing walks tail to head. Left-clicking a window unlinks it and appends it at the tail, making draw order and keyboard focus
change together. Closing the focused window focuses the new tail.

`SCR_DrawLayout()` draws persistent layout layers first and then calls `CL_WindowDraw()`, placing transient windows above the
HUD. Parsing and retaining `svc_window` does not make a window visible by itself; the screen layout pass owns submission.

`UI_WINDOW_MODAL` and `UI_WINDOW_UNIQUE` are independent. Modal means the topmost modal window consumes input outside its
bounds. Unique means only one instance of that class may exist. Inventory and quest-detail windows can be unique without being
modal; confirmation-style windows are modal.

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
