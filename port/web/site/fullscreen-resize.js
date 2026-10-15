/* Browser-only post-layout fullscreen/viewport synchronization.
 * OffscreenCanvas owns the rendering pixels; SDL/Emscripten owns its size.
 * Notify SDL after CSS layout stabilizes, never touch canvas.width/height here.
 */
(function (scope) {
  'use strict';
  function installFullscreenResize(win, doc, container, canvas) {
    if (!win || !doc || !container || !canvas) return;
    let queued = false;
    let force = false;
    let last = null;

    function measure() {
      const rect = canvas.getBoundingClientRect();
      if (!rect.width || !rect.height) return null;
      const fullscreen = doc.fullscreenElement === container ||
        doc.fullscreenElement === canvas;
      // Device pixel ratio matters when dragging windows between monitors.
      return [fullscreen ? 1 : 0, Math.round(rect.width * 100) / 100,
        Math.round(rect.height * 100) / 100, win.devicePixelRatio || 1].join(':');
    }

    function schedule(forceEvent) {
      force = force || !!forceEvent;
      if (queued) return;
      queued = true;
      // fullscreenchange can run before the new CSS dimensions are committed.
      // Compare two *successive* presentation frames to avoid notifying SDL
      // about the intermediate (old) size on the first fullscreen click.
      win.requestAnimationFrame(function () {
        const first = measure();
        win.requestAnimationFrame(function () {
          queued = false;
          const settled = measure();
          if (!settled) return; // Hidden canvas; ResizeObserver retries on show.
          if (first !== settled) {
            schedule(false); // Preserve the pending forced notification.
            return;
          }
          if (force || settled !== last) {
            force = false;
            last = settled;
            // Emscripten SDL2 listens for window resize and forwards it to
            // the engine worker; its window/drawable update remains authoritative.
            win.dispatchEvent(new win.Event('resize'));
          }
        });
      });
    }

    doc.addEventListener('fullscreenchange', function () { schedule(true); });
    win.addEventListener('resize', function () { schedule(false); });
    if (typeof win.ResizeObserver === 'function') {
      const observer = new win.ResizeObserver(function () { schedule(false); });
      observer.observe(container);
      observer.observe(canvas);
    }
    // The existing fullscreenchange handler calls this rather than dispatching
    // a resize synchronously, with a fallback for stale/unbundled pages.
    return {request: function () { schedule(true); }};
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {installFullscreenResize};
  } else {
    const controller = installFullscreenResize(scope, scope.document,
      scope.document.getElementById('game-container'),
      scope.document.getElementById('canvas'));
    scope.openrealmRequestFullscreenResize = controller && controller.request;
  }
})(globalThis);
