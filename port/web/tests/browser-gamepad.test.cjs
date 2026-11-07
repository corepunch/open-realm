/* Browser gamepad normalization regressions, with no DOM or game assets. */
const test = require('node:test');
const assert = require('node:assert/strict');
const { mapPad, BUTTON } = require('../site/browser-gamepad.js');

function pad(mapping = 'standard') {
  return {
    mapping, buttons: Array.from({ length: 17 }, () => ({ pressed: false, value: 0 })),
    axes: [0, 0, 0, 0]
  };
}

test('standard browser labels map to SDL2 logical positions without Nintendo swap', () => {
  const p = pad();
  p.buttons[0].pressed = true;
  p.buttons[4].pressed = true;
  p.buttons[12].pressed = true;
  p.buttons[7].value = 0.75;
  p.axes[1] = -1;
  const m = mapPad(p);
  assert.ok(m.buttons & (1 << BUTTON.A));
  assert.ok(m.buttons & (1 << BUTTON.LB));
  assert.ok(m.buttons & (1 << BUTTON.UP));
  assert.equal(m.axes[1], -32767);
  assert.equal(m.axes[5], Math.round(0.75 * 32767));
});

test('generic HID hat and trigger axes remain usable', () => {
  const p = pad('');
  p.buttons = p.buttons.slice(0, 6);
  p.axes = [0, 0, 0, 0, 1, -1, 0, 0, 0, -1];
  const m = mapPad(p);
  assert.ok(m.buttons & (1 << BUTTON.UP));
  assert.equal(m.axes[4], 32767);
  assert.equal(m.axes[5], 0);
});

test('malformed or missing axes resolve to a neutral state', () => {
  const p = pad();
  p.axes = [NaN, undefined, Infinity];
  assert.deepEqual(mapPad(p).axes, [0, 0, 0, 0, 0, 0]);
});


test('atomic mailbox latches a fast button press for the engine reader', () => {
  const { install } = require('../site/browser-gamepad.js');
  const heap = new Int32Array(new SharedArrayBuffer(128));
  const p = pad();
  p.index = 0;
  p.connected = true;
  let tick;
  const win = {
    navigator: { getGamepads: () => [p] }, SharedArrayBuffer,
    setInterval: fn => { tick = fn; }, addEventListener: () => {}
  };
  const doc = { hidden: false, hasFocus: () => true, addEventListener: () => {} };
  const mod = { HEAP32: heap, _Web_GamepadSharedAddress: () => 16 };
  assert.equal(install(win, doc, { hidden: false }, mod), true);
  tick();
  p.buttons[0].pressed = true;
  tick();
  p.buttons[0].pressed = false;
  tick();
  assert.equal(Atomics.load(heap, 16 / 4), 1);
  assert.equal(Atomics.load(heap, 16 / 4 + 1) & 1, 0);
  assert.equal(Atomics.load(heap, 16 / 4 + 2) & 1, 1);
});
