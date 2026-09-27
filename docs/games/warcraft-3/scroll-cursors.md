# Viewport scroll cursors

For the deeper cursor lifecycle, animation/tint state, virtual-call xrefs and
shared model-to-D3D path, see [retail cursor rendering](cursor-rendering.md).

## Retail contract

Verified against installed WC3 1.27.1.7085 (`game.dll` SHA256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`),
using an isolated Wine/Xvfb session, real mouse/keyboard input, Ghidra, and Frida.

- The arrow **replaces the cursor at the mouse hotspot**. It is not a separate
  indicator centered on a screen edge: moving along the right edge moves the
  arrow vertically with the pointer.
- The outermost client pixel activates mouse scrolling. Moving one pixel inward
  restores the normal cursor. Corners use one diagonal arrow.
- Arrow keys scroll without changing the cursor. Mouse direction selects the
  artwork, independently of world camera displacement (including map bounds).
- The cursor MDX contains the artwork, orientation, hotspot offset, and looping
  animation. Do not draw a triangle or rotate a generic texture in code.

`UI\Cursor\HumanCursor.mdx` contains these authored intervals (milliseconds):

| Sequence | Interval |
|---|---|
| Normal | 333–533 |
| Scroll Left | 4500–4700 |
| Scroll Right | 4833–5033 |
| Scroll Up | 5167–5367 |
| Scroll Down | 5500–5700 |
| Scroll Up Left | 5767–5967 |
| Scroll Up Right | 6100–6300 |
| Scroll Down Left | 6433–6633 |
| Scroll Down Right | 6767–6967 |

All eight scroll sequences loop for 200 ms. Read durations from the model rather
than duplicating that constant in rendering code.

## Binary evidence

Addresses below are virtual addresses at image base `0x6f000000`; relocate for
Frida and check the binary hash before reusing them.

- `0x6f361540`: camera input dispatcher. Mouse-edge events call `0x6f35fe60`
  with its fourth argument set to 1; arrow-key events pass 0.
- `0x6f35fe60`: direction bits 1=up, 2=down, 4=left, 8=right. The fourth
  argument controls cursor replacement. Cursor states 10–17 are left, right,
  up, down, up-left, up-right, down-left, down-right.
- `0x6f38a9c0`: selects the cursor animation tokens. Live Frida interception
  of `0x6f1a42b0` confirms Scroll token 6 plus direction tokens 7=left,
  8=right, 9=up, 10=down. This callee is **fastcall**: read ECX/EDX from
  `this.context`, not Frida's stack-based `args[0]`/`args[1]`.
- `0x6f3528e0`: registers the cursor animation tokens, including Scroll.

Retail captures and scratch traces from this investigation are under ignored
`build/scroll-arrow-audit/`; they are not test fixtures. The separate retail
session used display `:95`, a copy of the analysis Wine prefix in
`/tmp/openrealm-scroll-wine`, and a separate Frida server on port 27045 so the
existing audio analysis session was undisturbed.

Inspect authored sequences without launching a game:

```sh
build/bin/mdxtool -mpq "$WC3DATA/war3.mpq" \
  -model 'UI\Cursor\HumanCursor.mdx' --info
```

## Engine ownership

`CL_MouseScroll()` in `client/cl_input.c` computes the mouse-edge direction
once through a shared policy used by camera movement and cursor presentation.
It rejects inactive gameplay, console/modal input, loss of focus, touch pointers,
mouse-look, drag-pan, disabled edge scrolling, and points outside the window.
Keyboard camera buttons are deliberately absent from this query.

`SCR_DrawCursor()` passes the direction and UI hotspot in `drawCursor_t` through
`re.DrawCursor`. The WC3 renderer selects one of the authored sequences and
renders scrolling artwork white, without stale enemy/neutral hover tint. A
sequence change resets its retained frame before consuming the current UI step.
Loop completion restarts on the following update, matching retail’s queue
callback; non-looping sequences hold their endpoint. `drawSprite_t.start_time`
encodes that frame for the existing MDX path without changing scene time or
other sprite instances. The clock uses client presentation time while paused,
and the hotspot includes the D3D9-to-GL half-pixel correction. See the
[renderer investigation](cursor-rendering.md#engine-clock-and-framebuffer-corrections)
for the measured framebuffer differences and remaining precision gap. Missing cursor sequences are diagnosed once at load.

WC3 defaults enable `r_cursor 1` and set `cl_camera_edge_margin 0`. User overrides
remain supported: `r_cursor 0` selects the native SDL cursor; a larger edge margin
expands both scrolling and indicator activation. Other games retain their own
cursor behavior. The server now selects the cursor through the local race skin and map
`CustomSkin` override. Scrolling temporarily supersedes Target/HoldItem; see
[cursor state integration](cursor-rendering.md#race-targeting-and-held-item-integration).

These are local input and renderer changes: no network or save format changes.

## Verification

- `client_input.edge_scroll_cursor_lifecycle`: all eight directions, center,
  outermost pixel, keyboard exclusion, pointer outside the window, touch, focus,
  console, cinematic state, drag-pan, mouse-look, and option disable/restore.
- `mdx_ui.sprite_animation_epoch`: sequence start, wrapping, and unchanged scene
  time, using the production sprite draw path with GPU submission captured.
- Engine framebuffer captures `screenshots/shot0005.jpg` (right edge) and
  `shot0006.jpg` (top-left corner) were inspected against retail captures. They
  show the authored animated geometry at the mouse hotspot, including clipping
  at the screen boundary. Automated tests do not prove framebuffer parity.
- Baseline and final `make test` both crashed in the WC3 save-game suite on the
  development host. The targeted scroll, MDX, and camera tests passed; the broader
  input suite also crashed inside SDL's event queue in an unchanged test.

```sh
make test-mdx-ui
make openwarcraft3-tests
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test client_input.edge_scroll_cursor_lifecycle
```

See [shared input](../../architecture/shared-input.md) and
[retail tracing](retail-camera-tracing.md) for the surrounding workflows.
