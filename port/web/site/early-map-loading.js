/* Web-only map preparation overlay; server-authored loading artwork is untouched. */
(function (root) {
  'use strict';
  function makeEarlyMapLoading(document) {
    const overlay = document && document.getElementById('early-map-loading');
    let serial = 0;
    return function setEarlyMapLoading(visible) {
      if (!overlay) return;
      const request = ++serial;
      if (visible) {
        overlay.hidden = false;
        return;
      }
      // A worker can submit its WebGL frame before the browser composites it.
      // Keep the neutral indicator until at least one paint opportunity after
      // the authored loading frame was submitted. A newer map transition wins.
      const nextFrame = globalThis.requestAnimationFrame;
      if (typeof nextFrame !== 'function') {
        overlay.hidden = true;
        return;
      }
      nextFrame(() => nextFrame(() => {
        if (request === serial) overlay.hidden = true;
      }));
    };
  }
  if (typeof module !== 'undefined' && module.exports)
    module.exports = {makeEarlyMapLoading};
  else
    root.openrealmEarlyMapLoading = makeEarlyMapLoading(root.document);
})(globalThis);
