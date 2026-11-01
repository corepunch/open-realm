/* Browser-only press/drag/release overlays. The target is always the existing
 * SDL canvas keyboard path; neither menu implements game commands itself. */
(function (root) {
  'use strict';

  const functionCodes = {F9:120, F10:121, F11:122, F12:123};

  function installGameShortcutMenus(win, doc, canvas, menus) {
    if (!win || !doc || !canvas || !menus) return;
    let active = null;
    let target = null;
    let selectionUnlockTimer = null;

    function lockSelection() {
      if (selectionUnlockTimer !== null) {
        win.clearTimeout(selectionUnlockTimer);
        selectionUnlockTimer = null;
      }
      doc.documentElement.classList.add('game-shortcut-dragging');
    }

    function unlockSelection(immediately) {
      if (selectionUnlockTimer !== null) {
        win.clearTimeout(selectionUnlockTimer);
        selectionUnlockTimer = null;
      }
      if (immediately) {
        doc.documentElement.classList.remove('game-shortcut-dragging');
        return;
      }
      // iOS may begin native selection just after pointerup. Keep the page
      // protected briefly; a new drag cancels this pending unlock.
      selectionUnlockTimer = win.setTimeout(() => {
        selectionUnlockTimer = null;
        if (!active || active.pointerId === null)
          doc.documentElement.classList.remove('game-shortcut-dragging');
      }, 250);
    }

    function preventNativeGesture(event) {
      if (active && active.pointerId !== null && event.cancelable)
        event.preventDefault();
    }

    function keyboard(type, key, code, legacy, ctrl, shift) {
      const event = new win.KeyboardEvent(type, {
        key, code, keyCode:legacy, which:legacy,
        ctrlKey:ctrl, shiftKey:shift, altKey:false, metaKey:false,
        bubbles:true, cancelable:true
      });
      // WebKit and Chromium can ignore keyCode/which constructor properties;
      // Emscripten's SDL2 browser backend still reads those legacy values.
      for (const name of ['keyCode', 'which']) {
        if (event[name] !== legacy)
          Object.defineProperty(event, name, {get: () => legacy});
      }
      canvas.dispatchEvent(event);
    }

    function sendShortcut(button) {
      const key = button.getAttribute('data-game-key');
      const modifier = button.getAttribute('data-game-modifier') || '';
      const digit = /^[1-9]$/.test(key || '');
      if (!digit && !Object.prototype.hasOwnProperty.call(functionCodes, key)) return;
      if (modifier && (!digit || (modifier !== 'Control' && modifier !== 'Shift'))) return;
      const code = digit ? 'Digit' + key : key;
      const legacy = digit ? key.charCodeAt(0) : functionCodes[key];
      const ctrl = modifier === 'Control', shift = modifier === 'Shift';
      canvas.focus();
      if (modifier)
        keyboard('keydown', modifier, modifier + 'Left', ctrl ? 17 : 16, ctrl, shift);
      try {
        keyboard('keydown', key, code, legacy, ctrl, shift);
        keyboard('keyup', key, code, legacy, ctrl, shift);
      } finally {
        if (modifier)
          keyboard('keyup', modifier, modifier + 'Left', ctrl ? 17 : 16, false, false);
      }
    }

    function highlight(button) {
      if (button === target) return;
      if (target) target.classList.remove('is-target');
      target = button;
      if (target) target.classList.add('is-target');
    }

    function close(immediateUnlock = false) {
      highlight(null);
      if (!active) {
        if (immediateUnlock) unlockSelection(true);
        return;
      }
      const ownedPointer = active.pointerId !== null;
      active.button.setAttribute('aria-expanded', 'false');
      active.popup.hidden = true;
      active = null;
      if (ownedPointer || immediateUnlock) unlockSelection(immediateUnlock);
    }

    function open(button, popup, pointerId) {
      close();
      popup.hidden = false;
      button.setAttribute('aria-expanded', 'true');
      active = {button, popup, pointerId};
    }

    function shortcutAt(x, y) {
      if (!active || !doc.elementFromPoint) return null;
      const element = doc.elementFromPoint(x, y);
      const button = element && typeof element.closest === 'function' &&
        element.closest('button[data-game-key]');
      return button && active.popup.contains(button) ? button : null;
    }

    function pointerMove(event) {
      if (!active || event.pointerId !== active.pointerId) return;
      highlight(shortcutAt(event.clientX, event.clientY));
      if (event.cancelable) event.preventDefault();
    }

    function pointerEnd(event) {
      if (!active || event.pointerId !== active.pointerId) return;
      const selected = event.type === 'pointerup' ?
        shortcutAt(event.clientX, event.clientY) : null;
      close();
      if (event.cancelable) event.preventDefault();
      event.stopPropagation();
      if (selected) sendShortcut(selected);
    }

    function capturePointer(button, event) {
      lockSelection();
      // Capture applies to mouse/pen as well as touch, including a pointer
      // taking over a popup that was opened from the keyboard.
      if (typeof button.setPointerCapture === 'function') {
        try { button.setPointerCapture(event.pointerId); } catch (_) {
          // The document-level listeners still handle the gesture.
        }
      }
      if (event.cancelable) event.preventDefault();
      event.stopPropagation();
    }

    for (const [button, popup] of menus) {
      button.addEventListener('pointerdown', (event) => {
        if (active || (event.button !== undefined && event.button !== 0) ||
            event.isPrimary === false) return;
        open(button, popup, event.pointerId);
        capturePointer(button, event);
      });
      // Keyboard and assistive input can open the same popup without a drag.
      button.addEventListener('keydown', (event) => {
        if (event.key !== 'Enter' && event.key !== ' ') return;
        event.preventDefault();
        if (active && active.button === button) {
          close();
          return;
        }
        open(button, popup, null);
        const first = popup.querySelector('button[data-game-key]');
        if (first) first.focus();
      });
      button.addEventListener('lostpointercapture', (event) => {
        if (active && active.button === button && active.pointerId === event.pointerId)
          close();
      });
      popup.addEventListener('keydown', (event) => {
        if (event.key === 'Escape') {
          event.preventDefault();
          close();
          button.focus();
        } else if (event.key === 'Enter' || event.key === ' ') {
          const item = event.target.closest('button[data-game-key]');
          if (!item || !popup.contains(item)) return;
          event.preventDefault();
          close();
          sendShortcut(item);
        }
      });
    }
    doc.addEventListener('pointerdown', (event) => {
      if (!active || active.pointerId !== null) return;
      // A keyboard-open popup has no owning pointer until one presses inside.
      // Dismiss outside presses so it cannot block subsequent launcher drags.
      if (!active.popup.contains(event.target)) { close(); return; }
      if ((event.button !== undefined && event.button !== 0) || event.isPrimary === false) return;
      active.pointerId = event.pointerId;
      highlight(shortcutAt(event.clientX, event.clientY));
      capturePointer(active.button, event);
    }, true);
    doc.addEventListener('focusin', (event) => {
      if (active && active.pointerId === null && event.target !== active.button &&
          !active.popup.contains(event.target)) close();
    });
    // WebKit can still select nearby page text despite canceled pointer events.
    // These cancelable Touch Events and selection/callout guards apply only
    // while one of our popups owns a pointer, not to ordinary page controls.
    doc.addEventListener('touchstart', preventNativeGesture, {capture:true, passive:false});
    doc.addEventListener('touchmove', preventNativeGesture, {capture:true, passive:false});
    doc.addEventListener('selectstart', preventNativeGesture, true);
    doc.addEventListener('dragstart', preventNativeGesture, true);
    doc.addEventListener('contextmenu', preventNativeGesture, true);
    doc.addEventListener('pointermove', pointerMove, true);
    doc.addEventListener('pointerup', pointerEnd, true);
    doc.addEventListener('pointercancel', pointerEnd, true);
    win.addEventListener('blur', () => close(true));
    doc.addEventListener('visibilitychange', () => { if (doc.hidden) close(true); });
    return {close};
  }

  if (typeof module !== 'undefined' && module.exports)
    module.exports = {installGameShortcutMenus};
  else
    root.openrealmInstallGameShortcutMenus = installGameShortcutMenus;
})(globalThis);
