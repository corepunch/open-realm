# Browser gamepad input for Warcraft III

The web engine has a main-thread [Gamepad API](https://developer.mozilla.org/en-US/docs/Web/API/Gamepad_API)
sampler in `port/web/site/browser-gamepad.js`, a ten-word atomic Wasm memory
mailbox in `port/web/src/web_gamepad.c`, and a browser-only SDL2 event adapter
in `client/cl_input.c`. The native SDL2 controller path is not changed.

## Input lifecycle

1. A compatible wired/Bluetooth controller is connected. Some browsers only
   expose it to `navigator.getGamepads()` after a button press on the page.
2. While the game container is visible and the page focused, JS polls at 8 ms
   and normalizes standard Xbox/PlayStation/Nintendo mappings to the **SDL2**
   logical A/B/X/Y, D-pad, stick, bumper, and trigger indices. Unusual generic
   HID controllers may expose D-pad hats and trigger axes; these are decoded.
3. The page writes a compact snapshot to shared WebAssembly memory. Pressed
   button and trigger edges are OR-latched with `Atomics.or` until consumed by
   the engine pthread's `Web_GamepadRead` (`__atomic_exchange_n`). This avoids
   losing a quick press-and-release between game frames. Sampling waits for
   Emscripten's `Module.onRuntimeInitialized`; the exported address function
   exists before it is safe to call native code.
4. The native adapter synthesizes SDL2 button and axis events and calls the
   existing `CL_GamepadEvent` state machine. Thus the same selection, camera,
   radial menu, shortcuts, tooltips and hold/tap interactions run in browser
   and native. Only **one** controller backend drives web gameplay to prevent
   duplicate actions.
5. Disconnect, losing page focus, hiding the tab, or turning off `cl_gamepad`
   releases held state. The first connected browser controller is used; a
change in controller generation also clears prior held buttons.

JS does **not** swap Nintendo face-button labels. The archived
`cl_gamepad_nintendo` option applies that mapping once in the shared client.
No retail archives or controller identifiers are transmitted to a server.

## Shared memory contract

`Web_GamepadSharedAddress()` returns the byte offset of ten little-endian
32-bit words in the Emscripten `SharedArrayBuffer`:

| Word | Data |
|---:|---|
| 0 | connected flag |
| 1 | currently held SDL2 button bitset (bits 0–14) |
| 2 | latched down edges (bits 0–14 plus LT/RT bits 30/31) |
| 3 | generation number, increments when the active controller changes |
| 4–7 | LX, LY, RX, RY, signed 16-bit range encoded in i32 |
| 8–9 | LT, RT, 0–32767 |

The runtime must expose `Module.HEAP32` and `_Web_GamepadSharedAddress`.
`-pthread` and COOP/COEP isolation are required by the existing WebAssembly
build. If these are missing, the browser bridge logs a warning instead of
pretending the device is usable.

## Manual verification (not run by the patch author)

Pair an Xbox-style pad and press a button after opening the game page. Verify
left-stick camera pan, LB fast pan, right-stick zoom, paint selection, RT
control groups, LT hero shortcuts and the Y radial. Confirm A-to-accept,
B-to-cancel, RT radial paging, authored tooltips and Nintendo mapping. Try
pressing and releasing A/Y quickly between frames, unplugging during a held
button, switching tabs, and returning from a background page. Check Chrome,
Firefox and Safari where their Gamepad API and threaded WebAssembly support
permit it. Tests for JS mapping are under `port/web/tests/`.

No network wire format, game save format or authored command UI protocol changes.
