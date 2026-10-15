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
measurements. It posts a `window.resize` only when layout has settled.
A fallback from the HTML inline handler preserves legacy operation if the extra
script was not bundled.

The browser engine also independently synchronizes the logical SDL window size
and the worker-owned OffscreenCanvas backing resolution using the actual CSS
canvas size, because an HTML resize event by itself does not guarantee that
SDL2 updates either. The correction is performed by `renderer/r_main.c` at a
bounded 100 ms cadence using Emscripten's canvas APIs, not by assigning DOM
canvas width or height on the browser thread. `R_RefreshDrawableSize` queries
the Emscripten canvas backing dimensions rather than an out-of-date SDL cached
drawable size in web builds. The resulting SDL logical size drives the existing
`CL_CanvasFrame` resolution/settle and `ui_canvas` chrome updates.

This is browser-only. Native SDL fullscreen logic, WC3 FDF geometry, rendering
policies, keyboard capture and the OPFS installer are unchanged. Unlike the earlier event-only approach,
this patch explicitly adjusts the worker-side WebGL backing resolution. Browser
validation is still required because the Emscripten SDL2 sizing path may have
version-specific behaviour.

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

## Actual rendering size versus 4:3 skin policy

The browser's display box and the worker's pixel buffer are different sizes
until synchronized. Merely assigning `width: 100%; height: 100%` stretches an
old 4:3 image. The fullscreen CSS uses `!important` to preserve the viewport
box against inline SDL styles; the renderer then updates both the SDL logical
size and the actual worker-owned framebuffer. The game's `UI_ResolveCanvas`
receives the corrected aspect and the 250 ms settle gate reauthors widescreen
chrome, where supported by the installed skin. Classic Warcraft III archives
without the `ConsoleTexture05`/`06` extension tiles deliberately use the
`STRETCH` policy; this patch does not invent widescreen assets or change that
retail compatibility decision. This is web-only; it does not change native
SDL sizing or server-authored FDF progress bars.

The native window size must also match browser CSS pixel coordinates so mouse
hit-testing uses the same logical geometry. Browser DPR changes resynchronize
the worker-owned backing pixels without altering the logical UI dimensions.

After applying the patch, enter fullscreen once and check the renderer's
`Video: drawable size changed` message against the browser's CSS canvas box;
verify the `ui_canvas` transition for 1.30+ game archives, then exit and reenter
fullscreen, and check browser zoom/DPR behaviour. A real browser is needed to
establish whether the Emscripten SDL2 version reliably accepts the correction;
this cannot be proven by static checks alone.

## Windowed browser resizing and widescreen HUD transitions

Fullscreen sizing is a separate case from resizing the **ordinary browser
window**. Previously, `engine-shell.html` forced the windowed canvas to
`aspect-ratio: 16 / 9`. Changing the browser width therefore changed the
canvas height proportionally: OpenRealm kept seeing the same logical aspect,
so the `UI_CANVAS_STANDARD`/`UI_CANVAS_WIDE` boundary could never be crossed.
For browsers where SDL wrote inline canvas dimensions, the CSS rules could
also stop following the actual windowed-page layout.

The windowed canvas instead uses 100% of its container width and a bounded,
viewport-relative height. Its CSS aspect is now permitted to vary with the
browser window. The browser renderer continues to sample the CSS box and sets
both SDL logical dimensions and the worker's pixel backing store as before;
`CL_CanvasFrame` alone owns the 250 ms settle and `ui_canvas` command. CSS uses
`!important` because SDL may write inline width/height styles. The fullscreen
rules are unchanged and take over when the container enters fullscreen.

**Expected behaviour:** with widescreen-capable game assets, make the browser
window narrow enough for a 4:3 canvas, then widen it past the canvas class
threshold and keep it there for 250 ms: the HUD extensions should appear.
Reverse the resize and the extensions should disappear. At resolutions that
remain widescreen on both sides of the resize, the widescreen UI class should
not flap or unnecessarily reload. Classic archives without `ConsoleTexture05`
continue to use the retail-derived stretch policy; this CSS fix does not
fabricate missing widescreen assets. Test this in a real browser as well as
running the synthetic regression in `port/web/tests/fullscreen-resize.test.cjs`.
