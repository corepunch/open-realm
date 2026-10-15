# Browser fullscreen canvas size synchronization

The Warcraft III browser launcher enters DOM fullscreen on `#game-container`,
not on `#canvas`: fullscreen must keep the canvas, Escape button and fullscreen
button in the same browser top-layer element. The engine owns a WebGL 2 context
on a worker-transferred `OffscreenCanvas` (`PROXY_TO_PTHREAD`), so the DOM main
thread **must not write canvas.width / canvas.height** after transfer.

## First-entry fullscreen sizing

A `fullscreenchange` event can arrive before the browser commits the new CSS
layout. An immediate synthetic `window.resize` may tell Emscripten's SDL2
window backend to recompute while the canvas still has its old dimensions.
This can leave the game stretched or clipped on the first attempt; subsequent
layout changes happen to correct it.

`port/web/site/fullscreen-resize.js` watches both the canvas and its fullscreen
container using `ResizeObserver`, then coalesces events into two animation-frame
measurements. It posts a `window.resize` only when layout has settled. The
normal SDL resize handling updates the window and rendering target on the engine
thread; existing renderer/client paths (`R_WindowChanged`,
`R_RefreshDrawableSize`, `CL_CanvasWindowChanged`) remain responsible for
viewport and UI geometry. A fallback from the HTML inline handler preserves
legacy operation if the extra script was not bundled.

This is browser-only. Native SDL fullscreen logic, WC3 FDF geometry, rendering
policies, keyboard capture and the OPFS installer are unchanged. It does not
change WebGL drawing resolution itself: it ensures Emscripten sees the final CSS
size after the transition. If a browser/SDL version fails to resize the backing
store even after the final resize event, an engine-side canvas-size correction
will need separate investigation.

## Verification

Run the synthetic event-ordering regression:

```sh
node --test port/web/tests/fullscreen-resize.test.cjs
```

Then in Chrome/Edge check initial fullscreen entry, exit, repeated entry,
HiDPI and browser-zoom changes. The first fullscreen click should fill the
container and have correct mouse-hit geometry without a second click. Test
without development tools and with developer tools open; browser rendering
must still be checked manually. Build and game/runtime tests remain the
contributor's responsibility.
