/* Web-only map preparation overlay; server-authored loading artwork is untouched. */
(function (root) {
  'use strict';
  function makeEarlyMapLoading(document) {
    const overlay = document && document.getElementById('early-map-loading');
    let serial = 0;
    return function setEarlyMapLoading(active, progress, loadingScreenSubmitted) {
      if (!overlay) return;
      const request = ++serial;
      // Loading layouts arrive before map registration is finished. Keep the
      // browser placeholder until the authored loading frame is ready and the
      // client reaches the existing post-RegisterMap 40% checkpoint.
      if (active && (!loadingScreenSubmitted || progress < 0.4)) {
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
