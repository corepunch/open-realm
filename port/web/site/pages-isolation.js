/* GitHub Pages-only cross-origin isolation bootstrap for /openwarcraft3/play/.
 * This script runs before the OPFS mount and game launcher, but is not a
 * replacement for COOP/COEP response headers on the original navigation.
 */
(function (root) {
  'use strict';
  const PATH = '/openwarcraft3/play/';
  const RELOAD_KEY = 'openrealm-pages-coi-reloaded-v1';

  function pageMatches(pathname) {
    return pathname === PATH || pathname === PATH + 'index.html';
  }

  function status(win, message) {
    const node = win.document && win.document.getElementById('status');
    if (node) node.textContent = message;
  }

  function controllerIsOurs(worker, scriptURL) {
    return !!(worker.controller && worker.controller.scriptURL === scriptURL);
  }

  function waitForControl(worker, scriptURL) {
    if (controllerIsOurs(worker, scriptURL)) return Promise.resolve(true);
    return new Promise(function (resolve) {
      let done = false;
      function finish(value) {
        if (done) return;
        done = true;
        worker.removeEventListener('controllerchange', changed);
        clearTimeout(timer);
        resolve(value);
      }
      function changed() {
        if (controllerIsOurs(worker, scriptURL)) finish(true);
      }
      worker.addEventListener('controllerchange', changed);
      const timer = setTimeout(function () { finish(false); }, 10000);
      changed();
    });
  }

  function waitForActivation(worker, timeoutMs) {
    return new Promise(function (resolve) {
      let done = false;
      function finish(value) {
        if (done) return;
        done = true;
        clearTimeout(timer);
        resolve(value);
      }
      const timer = setTimeout(function () { finish(false); }, timeoutMs === undefined ? 10000 : timeoutMs);
      worker.ready.then(function () { finish(true); }, function () { finish(false); });
    });
  }

  async function ensurePagesIsolation(win) {
    if (!pageMatches(win.location.pathname)) return true;
    const worker = win.navigator && win.navigator.serviceWorker;
    if (!win.isSecureContext || !worker) {
      status(win, 'The browser needs HTTPS and Service Worker support to run Warcraft III.');
      return false;
    }
    const scriptURL = new URL('coi-serviceworker.js', win.location.href).href;
    try {
      await worker.register(scriptURL, {scope: PATH});
      // On first visit, the new worker must activate and claim the game page.
      if (!(await waitForActivation(worker))) {
        status(win, 'The game Service Worker did not activate. Check browser support and reload to retry.');
        return false;
      }
      if (win.crossOriginIsolated) {
        try { win.sessionStorage.removeItem(RELOAD_KEY); } catch (_) {}
        return true;
      }
      // Even a claiming worker cannot retrofit COOP/COEP to this document:
      // it must be reloaded so the SW can add headers to the navigation.
      if (!(await waitForControl(worker, scriptURL))) {
        status(win, 'The game Service Worker did not take control. Reload the page to retry.');
        return false;
      }
      let reloaded = false;
      try {
        reloaded = win.sessionStorage.getItem(RELOAD_KEY) === scriptURL;
        if (!reloaded) win.sessionStorage.setItem(RELOAD_KEY, scriptURL);
      } catch (_) {
        status(win, 'Allow session storage and reload this page to enable WebAssembly threads.');
        return false;
      }
      if (reloaded) {
        status(win, 'Cross-origin isolation is unavailable after reload. Check Service Worker support and reload manually.');
        return false;
      }
      status(win, 'Enabling browser isolation for Warcraft III…');
      win.location.reload();
      return false;
    } catch (error) {
      status(win, 'Could not enable cross-origin isolation: ' + error.message);
      return false;
    }
  }

  if (typeof module === 'object' && module.exports)
    module.exports = {pageMatches, ensurePagesIsolation, waitForActivation};
  else
    root.openrealmPagesReady = ensurePagesIsolation(root);
})(globalThis);
