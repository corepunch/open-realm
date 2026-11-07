/* Browser Gamepad API -> WebAssembly shared memory. The engine worker owns
 * all controller semantics; JS only normalizes hardware to SDL2 button/axis
 * numbers. Do not also send these events through Emscripten SDL's joystick
 * backend: the WC3 web client uses this as its single controller source. */
'use strict';
(function (root) {
  const BUTTON = {
    A: 0, B: 1, X: 2, Y: 3, BACK: 4, GUIDE: 5, START: 6,
    LS: 7, RS: 8, LB: 9, RB: 10, UP: 11, DOWN: 12, LEFT: 13, RIGHT: 14
  };
  const WORD = { CONNECTED: 0, BUTTONS: 1, PRESSED: 2, GENERATION: 3, AXES: 4 };
  const TRIG_LEFT = (1 << 30) >>> 0;
  const TRIG_RIGHT = (1 << 31) >>> 0;
  const ALL_BUTTONS = (1 << 15) - 1;
  const SAMPLE_MS = 8;
  const clamp = (value, lo, hi) => Math.max(lo, Math.min(hi, value));
  const axis = value => Math.round(clamp(Number.isFinite(value) ? value : 0, -1, 1) * 32767);
  const trigger = value => Math.round(clamp(Number.isFinite(value) ? value : 0, 0, 1) * 32767);

  function mapPad(pad) {
    const b = pad.buttons || [], a = pad.axes || [];
    const pressed = i => !!(b[i] && (b[i].pressed || b[i].value > 0.5));
    const value = i => b[i] ? clamp(Number(b[i].value) || (b[i].pressed ? 1 : 0), 0, 1) : 0;
    const map = [[0, BUTTON.A], [1, BUTTON.B], [2, BUTTON.X], [3, BUTTON.Y],
      [4, BUTTON.LB], [5, BUTTON.RB], [8, BUTTON.BACK], [9, BUTTON.START],
      [10, BUTTON.LS], [11, BUTTON.RS], [12, BUTTON.UP], [13, BUTTON.DOWN],
      [14, BUTTON.LEFT], [15, BUTTON.RIGHT], [16, BUTTON.GUIDE]];
    let buttons = 0;
    for (const [index, bit] of map) if (pressed(index)) buttons |= 1 << bit;
    let lt = value(6), rt = value(7);
    if (pad.mapping !== 'standard') {
      /* Some generic HID pads expose triggers as axes -1..1 and a hat D-pad. */
      if (b.length < 8 && a.length >= 6) {
        lt = clamp((a[4] + 1) / 2, 0, 1);
        rt = clamp((a[5] + 1) / 2, 0, 1);
      }
      if (b.length < 13) {
        let x = 0, y = 0;
        if (a.length >= 10 && a[9] > -1.01 && a[9] < 1.01) {
          const step = Math.round((a[9] + 1) * 3.5);
          x = [0, 1, 1, 1, 0, -1, -1, -1][step] || 0;
          y = [-1, -1, 0, 1, 1, 1, 0, -1][step] || 0;
        } else if (a.length >= 8) {
          x = Math.round(a[6]); y = Math.round(a[7]);
        }
        if (y < 0) buttons |= 1 << BUTTON.UP;
        if (y > 0) buttons |= 1 << BUTTON.DOWN;
        if (x < 0) buttons |= 1 << BUTTON.LEFT;
        if (x > 0) buttons |= 1 << BUTTON.RIGHT;
      }
    }
    return {
      buttons: buttons & ALL_BUTTONS,
      axes: [axis(a[0]), axis(a[1]), axis(a[2]), axis(a[3]), trigger(lt), trigger(rt)]
    };
  }

  function install(win, doc, container, module) {
    if (!win.navigator || typeof win.navigator.getGamepads !== 'function') return false;
    let previous = 0;
    let generation = 0;
    let activeIndex = -1;
    let address = 0;
    let warned = false;
    let runtimeReady = false;
    const onRuntimeInitialized = module.onRuntimeInitialized;
    module.onRuntimeInitialized = function () {
      runtimeReady = true;
      if (typeof onRuntimeInitialized === 'function')
        onRuntimeInitialized.apply(this, arguments);
    };
    function sample() {
      /* Emscripten publishes native exports before initializing their runtime.
       * Polling must wait for its lifecycle callback before calling C. */
      if (!runtimeReady) return;
      if (typeof module._Web_GamepadSharedAddress !== 'function' || !module.HEAP32) return;
      if (!address) address = module._Web_GamepadSharedAddress();
      const heap = module.HEAP32;
      if (!address || typeof win.SharedArrayBuffer !== 'function' ||
          !(heap.buffer instanceof win.SharedArrayBuffer)) {
        if (!warned) { console.warn('OpenRealm gamepad: shared WASM memory unavailable'); warned = true; }
        return;
      }
      const base = address >>> 2;
      if (base + WORD.AXES + 6 > heap.length) return;
      /* A hidden/unfocused page must release held commands, rather than leave
       * an RT or Y pressed while the tab is in the background. */
      const playing = !doc.hidden && doc.hasFocus() && !container.hidden;
      let pad = null;
      if (playing) {
        const pads = win.navigator.getGamepads() || [];
        pad = activeIndex >= 0 ? pads[activeIndex] : null;
        if (!pad || !pad.connected) pad = Array.from(pads).find(p => p && p.connected) || null;
      }
      const index = pad ? pad.index : -1;
      if (index !== activeIndex) {
        previous = 0;
        generation = (generation + 1) >>> 0;
      }
      activeIndex = index;
      const mapped = pad ? mapPad(pad) : { buttons: 0, axes: [0, 0, 0, 0, 0, 0] };
      const bits = mapped.buttons |
        (mapped.axes[4] > 16384 ? TRIG_LEFT : 0) |
        (mapped.axes[5] > 16384 ? TRIG_RIGHT : 0);
      const fresh = (bits & ~previous) >>> 0;
      previous = bits >>> 0;
      /* Keep edge writes atomic across the page/worker boundary; latched
       * presses survive a press+release between two native game frames. */
      Atomics.store(heap, base + WORD.BUTTONS, mapped.buttons);
      mapped.axes.forEach((v, i) => Atomics.store(heap, base + WORD.AXES + i, v));
      if (fresh) Atomics.or(heap, base + WORD.PRESSED, fresh | 0);
      Atomics.store(heap, base + WORD.GENERATION, generation | 0);
      Atomics.store(heap, base + WORD.CONNECTED, pad ? 1 : 0);
    }
    win.setInterval(sample, SAMPLE_MS);
    win.addEventListener('gamepadconnected', sample);
    win.addEventListener('gamepaddisconnected', sample);
    win.addEventListener('blur', sample);
    doc.addEventListener('visibilitychange', sample);
    return true;
  }
  root.openrealmInstallBrowserGamepad = install;
  if (typeof module !== 'undefined' && module.exports) module.exports = { mapPad, install, BUTTON };
})(typeof window === 'undefined' ? globalThis : window);
