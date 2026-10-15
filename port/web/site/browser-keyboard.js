/* Browser keyboard ownership for the active Warcraft III canvas.
 * Keep real events flowing to Emscripten/SDL; only suppress browser defaults. */
(function (root) {
  'use strict';
  function installKeyboardCapture(win, doc, canvas, container) {
    if (!win || !doc || !canvas || !container) return;
    const down = new Map();
    const modifierCodes = new Set(['ControlLeft', 'ControlRight', 'ShiftLeft', 'ShiftRight']);
    const browserReserved = new Set(['KeyW', 'KeyT', 'KeyN', 'KeyL', 'KeyR', 'KeyQ']);
    let engaged = false;
    let locked = false;
    const fullscreen = () => doc.fullscreenElement === container ||
      doc.fullscreenElement === canvas;
    function ownsKeyboard() {
      return engaged && (doc.activeElement === canvas || fullscreen());
    }
    function releaseHeld() {
      for (const [code, info] of down) {
        const event = new win.KeyboardEvent('keyup', {
          key: info.key, code, bubbles: true, cancelable: true,
          ctrlKey: false, shiftKey: false, altKey: false, metaKey: false
        });
        canvas.dispatchEvent(event);
      }
      down.clear();
    }
    function key(event) {
      if (!ownsKeyboard() || event.isComposing) return;
      const target = event.target;
      if (target && target !== canvas &&
          (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT|BUTTON)$/.test(target.tagName || '')))
        return;
      const code = event.code || event.key;
      if (event.type === 'keyup') down.delete(code);
      else if (!event.repeat && modifierCodes.has(code)) {
        down.set(code, {key:event.key});
      }
      // Don't hijack system/browser shortcuts. Those cannot reliably be
      // captured by a normal webpage, even when the canvas is focused.
      if (event.metaKey || event.altKey) return;
      if (event.ctrlKey && browserReserved.has(code)) return;
      if (code === 'Escape' || modifierCodes.has(code) ||
          (event.ctrlKey && /^Digit[0-9]$/.test(code)) ||
          (event.shiftKey && /^Digit[0-9]$/.test(code)))
        if (event.cancelable) event.preventDefault();
      // Do not stopPropagation: SDL/Emscripten still needs this real event.
    }
    async function syncLock() {
      const api = win.navigator && win.navigator.keyboard;
      if (!api || typeof api.lock !== 'function') return;
      if (!fullscreen() || !engaged) {
        if (locked && typeof api.unlock === 'function') api.unlock();
        locked = false;
        return;
      }
      try {
        await api.lock(['Escape', 'ControlLeft', 'ControlRight', 'ShiftLeft', 'ShiftRight']);
        locked = true;
      } catch (_) {
        locked = false; // Unsupported key/browser: standard event fallback.
      }
    }
    canvas.addEventListener('pointerdown', () => {
      engaged = true;
      canvas.focus();
      void syncLock();
    });
    canvas.addEventListener('focus', () => { engaged = true; });
    canvas.addEventListener('blur', () => {
      engaged = false;
      releaseHeld();
      void syncLock();
    });
    win.addEventListener('keydown', key, true);
    win.addEventListener('keyup', key, true);
    win.addEventListener('blur', () => {
      engaged = false;
      releaseHeld();
      void syncLock();
    });
    doc.addEventListener('visibilitychange', () => {
      if (doc.hidden) {
        engaged = false;
        releaseHeld();
        void syncLock();
      }
    });
    doc.addEventListener('fullscreenchange', () => {
      if (fullscreen()) {
        engaged = true;
        canvas.focus();
      } else {
        releaseHeld();
      }
      void syncLock();
    });
    return {releaseHeld};
  }
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = {installKeyboardCapture};
  } else {
    root.openrealmInstallKeyboardCapture = installKeyboardCapture;
  }
})(globalThis);
