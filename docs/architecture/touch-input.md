# SDL2 touch input and hybrid devices

## Ownership and contract

`client/cl_input.c` owns the shared client-side mouse state and direct touch
contacts, irrespective of which game is running. The engine does not switch
between a "desktop" and a "touch" mode. SDL mouse and keyboard events retain
their established routing through bindings, the main menu, retained windows,
HUD hit testing, and generic game input. The touch path generates the **same
internal mouse/UI events** for one-finger interactions; no touch-specific game
commands or protocol extensions are introduced.

At `CL_InitInput`, both `SDL_HINT_TOUCH_MOUSE_EVENTS` and
`SDL_HINT_MOUSE_TOUCH_EVENTS` are set to `"0"`. This prevents SDL from creating
extra events in either direction. SDL finger events remain available. Defensive
`SDL_TOUCH_MOUSEID` filtering ignores a backend-synthesized mouse event if one
is nevertheless encountered; internal translated touches have a separate flag
so they still reach the usual handlers.

- A **direct** first contact becomes a left-button press at the finger's
  normalized coordinates, scaled by `re.GetWindowSize()`. Moving that contact
  becomes mouse motion; releasing it becomes a left-button release.
- A **second direct finger**, while gameplay owns input and the first contact
  began outside menu/HUD controls, cancels selection and releases the first
  contact's held left binding without completing a selection. The gesture
  initially remains eligible for a two-finger right click.
- If both fingers lift within **350 ms** of the first finger-down, and neither
  travels more than **2% of the shorter SDL window axis** after the second
  contact, the gesture sends exactly one normal right-button press and release
  at the initial two-finger midpoint. Dragging the first finger before the
  second arrives also disqualifies a right click. A third finger, real mouse
  drag, UI-started contact, slow gesture or lost focus cannot cause the click.
- Moving either finger beyond the threshold starts the existing midpoint
  camera pan, anchored at the midpoint captured when the second finger landed.
  The threshold avoids camera jitter from stationary taps while preserving
  the full movement when panning begins.
- A pinch requires **both fingers moving in opposing directions along their
  separation axis**, not merely a temporarily changed distance. Its initial
  separation change must exceed **4% of the shorter window axis**, with at
  least **1.2% of that axis** of opposing travel by each finger. After zoom is
  active, smaller opposing moves can continue it (1% separation deadband).
  This deliberately rejects a one-finger-only stretch, reducing accidental
  zoom during ordinary two-finger panning. Both fingers must start at least
  **4% of the shorter axis** apart to avoid unstable near-zero spacing.
  Pure translation is removed from the pinch movement baseline, so intentional
  pinches still work after a pan, or alongside perpendicular panning. A pinch
  permanently disqualifies a two-finger right-click tap.
- Pinch scales the current camera distance proportionally to the accepted
  contact-separation ratio, with archived `cl_touch_pinch_sensitivity` (default
  `0.3`, clamped to `[0, 4]`; `0` disables zoom). Pinch, mouse wheel and zoom
  hotkeys all use `CL_GameCameraZoomPolicy`/generic player zoom limits; JASS
  scripted camera fields are unaffected. Motions are coalesced per SDL input
  poll, and deliberate opposing movement is required even when SDL sends the
  two fingers' motion events in separate polls.
- After a two-finger gesture, the remaining contact cannot accidentally click.
  The gesture is cleared after all tracked fingers are released.
- `SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE` / indirect trackpads are not treated as
  a touchscreen: their ordinary mouse, wheel, and keyboard streams still work.
- A real mouse button drag has priority over new finger presses. While a finger
  holds the shared left-button UI pointer, real mouse button/motion events do
  not overwrite that drag. The physical mouse resumes on release; while a
  two-finger pan is active, real mouse hover must not feed camera panning.
  The shared UI pointer does not support *two simultaneous independent drags*.
- Losing SDL window focus drops tracked contacts and held pointer bindings.
  The same touch handling works in menu and gameplay; the second-finger pan is
  gated by `CL_GameplayInputReady()`.

`mouse.origin`, mouse key bindings, `SCR_LayoutMouseEvent`,
`CL_WindowMouseEventWithMods`, and `menu.MouseEvent` remain the one authoritative shared UI/pointer contract.
Touch must not implement its own game-side hit testing or command dispatcher.

## Browser builds

The Emscripten SDL2 build uses the identical SDL event pump. The engine page's
`#game-container > canvas` has `touch-action: none` so dragging/zooming on the
game canvas is delivered to SDL rather than consumed as page gestures. Escape
and fullscreen overlay controls retain `touch-action: manipulation` so their
normal button interactions work. This is canvas-only: scrolling other page
controls is unaffected.

The canvas's SDL logical size and CSS display size can differ; use the
renderer-provided SDL window size and SDL's normalized finger positions, not
browser `pageX/pageY` or backing WebGL pixel dimensions. Physical hybrid
hardware and browser backends still require runtime verification.

## Verification

Input tests live in `client/cl_input.c`:

- `client_input.two_finger_touch_pans_camera`: SDL-finger-only press/pan/release,
  duplicate synthetic mouse-event suppression, and edge-scroll handoff.
- `client_input.two_finger_tap_sends_right_click`: quick taps (including
  lift-order and small-motion cases), slow taps, third-contact cancellation,
  and the ordinary bound right-click command at the finger midpoint.
- `client_input.two_finger_pinch_zoom_respects_camera_policy`: zoom directions,
  proportional scaling, minimum/maximum camera distance, and no click on pinch release.
- `client_input.two_finger_translation_does_not_zoom_and_focus_cancels_pinch`:
  pure two-finger pan, including staggered SDL motion across separate polls,
  produces no zoom; a deliberate opposing pinch still works after that pan,
  and losing focus cancels pending zoom.
- `client_input.hybrid_mouse_and_touch_preserve_press_owner`: real mouse held
  while touch arrives, then touch press/drag after mouse release, followed by
  mouse movement after touch release.

Run the regular engine test suite (`make test`) on a native development host,
then check touch taps, HUD clicks, drag selection, two-finger panning and mouse
+ keyboard use on a touchscreen laptop, as well as a browser engine build on a
real touch device. Automated SDL event injection does not prove browser
pointer-event and CSS behavior, or the OS's touch device classification.
