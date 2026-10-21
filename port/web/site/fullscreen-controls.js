/* Browser-only fullscreen controls. Safari may expose only prefixed WebKit
 * APIs, whose request/exit methods can return void rather than a Promise. */
(function (root) {
  'use strict';

  function fullscreenElement(doc) {
    return doc.fullscreenElement || doc.webkitFullscreenElement || null;
  }

  function installFullscreenControls(win, doc, container, canvas, button, status) {
    if (!win || !doc || !container || !canvas || !button) return;

    function showError(error) {
      if (status) {
        status.textContent = 'Fullscreen unavailable: ' +
          (error && error.message ? error.message : 'this browser does not support game fullscreen');
      }
    }

    function invoke(target, standardName, webkitName, options) {
      const fn = target[standardName] || target[webkitName];
      if (typeof fn !== 'function') return {supported: false};
      try {
        const result = fn.call(target, options);
        // Older Safari's webkitRequestFullscreen returns undefined.
        if (result && typeof result.catch === 'function') result.catch(showError);
        return {supported: true, result};
      } catch (error) {
        showError(error);
        return {supported: true};
      }
    }

    function request(options) {
      const call = invoke(container, 'requestFullscreen', 'webkitRequestFullscreen', options);
      if (!call.supported) showError(null);
      return call.result;
    }

    function exit() {
      const call = invoke(doc, 'exitFullscreen', 'webkitExitFullscreen');
      if (!call.supported) showError(null);
      return call.result;
    }

    // Emscripten may request fullscreen directly on the canvas. Redirect it
    // to the container so Escape and fullscreen controls remain visible.
    for (const method of ['requestFullscreen', 'webkitRequestFullscreen']) {
      if (typeof canvas[method] !== 'function') continue;
      try {
        canvas[method] = function (options) { return request(options); };
      } catch (_) { /* Non-writable native method; the page button still works. */ }
    }

    function sync(notifyResize) {
      const element = fullscreenElement(doc);
      const active = element === container || element === canvas;
      container.classList.toggle('is-fullscreen', active);
      button.setAttribute('aria-label', active ? 'Exit fullscreen' : 'Enter fullscreen');
      button.title = active ? 'Exit fullscreen' : 'Toggle fullscreen';
      if (notifyResize) {
        if (typeof win.openrealmRequestFullscreenResize === 'function')
          win.openrealmRequestFullscreenResize();
        else
          win.dispatchEvent(new win.Event('resize'));
      }
    }

    button.addEventListener('click', function (event) {
      event.stopPropagation();
      const element = fullscreenElement(doc);
      if (element === container || element === canvas) exit();
      else request();
    });
    doc.addEventListener('fullscreenchange', function () { sync(true); });
    doc.addEventListener('webkitfullscreenchange', function () { sync(true); });
    sync(false);
    return {request, exit};
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {fullscreenElement, installFullscreenControls};
  } else {
    root.openrealmInstallFullscreenControls = installFullscreenControls;
  }
})(globalThis);
