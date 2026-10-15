/* Browser keyboard capture tests: synthetic DOM events, no Warcraft III data. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {installKeyboardCapture} = require('../site/browser-keyboard.js');

function target() {
  const handlers = new Map();
  return {
    addEventListener(type, handler) {
      if (!handlers.has(type)) handlers.set(type, []);
      handlers.get(type).push(handler);
    },
    fire(type, event = {}) {
      for (const handler of handlers.get(type) || []) handler(event);
    }
  };
}
function fixture() {
  const win = target(), doc = target(), canvas = target(), container = {};
  const released = [];
  let locked = 0, unlocked = 0;
  win.KeyboardEvent = class {
    constructor(type, options) { this.type=type; Object.assign(this, options); }
  };
  win.navigator = {keyboard:{
    async lock() { locked++; },
    unlock() { unlocked++; }
  }};
  doc.fullscreenElement = null;
  doc.activeElement = null;
  canvas.focus = () => { doc.activeElement = canvas; };
  canvas.dispatchEvent = event => released.push(event);
  installKeyboardCapture(win, doc, canvas, container);
  function key(type, code, options = {}) {
    const event = {type, code, key:options.key || code, cancelable:true,
      repeat:false, ctrlKey:false, shiftKey:false, altKey:false, metaKey:false,
      target:canvas, prevented:false, preventDefault() { this.prevented = true; },
      ...options};
    win.fire(type, event);
    return event;
  }
  return {win, doc, canvas, container, key, released,
    get locked() { return locked; }, get unlocked() { return unlocked; }};
}
test('keyboard events are not hijacked until the game has focus', () => {
  const f=fixture();
  assert.equal(f.key('keydown','Escape').prevented, false);
  f.canvas.fire('pointerdown');
  assert.equal(f.key('keydown','Escape').prevented, true);
  assert.equal(f.key('keydown','ControlLeft',{key:'Control'}).prevented, true);
  assert.equal(f.key('keydown','ShiftLeft',{key:'Shift'}).prevented, true);
  assert.equal(f.key('keydown','Digit1',{ctrlKey:true}).prevented, true);
  assert.equal(f.key('keydown','KeyW',{ctrlKey:true}).prevented, false);
});
test('interactive page fields keep their keyboard shortcuts', () => {
  const f=fixture();
  f.canvas.fire('pointerdown');
  assert.equal(f.key('keydown','Escape',{target:{tagName:'INPUT'}}).prevented, false);
});
test('window blur releases held modifiers through SDL event path', () => {
  const f=fixture();
  f.canvas.fire('pointerdown');
  f.key('keydown','ControlLeft',{key:'Control'});
  f.key('keydown','ShiftLeft',{key:'Shift'});
  f.win.fire('blur');
  assert.deepEqual(f.released.map(e => [e.type,e.code]),
    [['keyup','ControlLeft'],['keyup','ShiftLeft']]);
  assert.equal(f.key('keydown','Escape').prevented, false);
});
test('fullscreen requests keyboard lock and releases it on exit', async () => {
  const f=fixture();
  f.doc.fullscreenElement=f.container;
  f.doc.fire('fullscreenchange');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.locked,1);
  assert.equal(f.key('keydown','Escape').prevented, true);
  f.doc.fullscreenElement=null;
  f.doc.fire('fullscreenchange');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(f.unlocked,1);
});
