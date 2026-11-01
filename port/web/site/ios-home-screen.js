/* iOS Safari Home Screen app guide and installed-app presentation.
 * No automatic install prompt exists on Safari; instructions are user-driven. */
(function (root) {
  'use strict';

  function isIOSDevice(nav) {
    if (!nav) return false;
    return /iPhone|iPad|iPod/i.test(nav.userAgent || '') ||
      (nav.platform === 'MacIntel' && nav.maxTouchPoints > 1);
  }

  function isIOSSafari(nav) {
    if (!isIOSDevice(nav)) return false;
    const ua = nav.userAgent || '';
    return /Safari/i.test(ua) && /Version\//i.test(ua) &&
      !/CriOS|FxiOS|EdgiOS|OPiOS|DuckDuckGo|GSA/i.test(ua);
  }

  function isStandalone(win) {
    const media = win.matchMedia;
    return !!(win.navigator && win.navigator.standalone) ||
      (typeof media === 'function' && (
        media.call(win, '(display-mode: standalone)').matches ||
        media.call(win, '(display-mode: fullscreen)').matches));
  }

  function installIOSHomeScreen(win, doc) {
    if (!win || !doc) return null;
    const ios = isIOSDevice(win.navigator);
    const safari = isIOSSafari(win.navigator);
    const installed = ios && isStandalone(win);
    const panel = doc.getElementById('ios-app-guide');
    const dismiss = doc.getElementById('ios-app-guide-dismiss');
    doc.body.classList.toggle('ios-web-app', installed);
    if (panel) panel.hidden = !safari || installed;

    function showGuide() {
      if (!safari || installed || !panel) return false;
      panel.hidden = false;
      if (typeof panel.scrollIntoView === 'function')
        panel.scrollIntoView({block: 'start', behavior: 'smooth'});
      return true;
    }

    if (dismiss) dismiss.addEventListener('click', function () {
      if (panel) panel.hidden = true;
    });
    // The fullscreen control uses this handler only on iOS Safari tabs.
    win.openrealmShowIOSInstallGuide = showGuide;
    return {ios, safari, installed, showGuide};
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {isIOSDevice, isIOSSafari, isStandalone, installIOSHomeScreen};
  } else {
    root.openrealmInstallIOSHomeScreen = installIOSHomeScreen;
  }
})(globalThis);
