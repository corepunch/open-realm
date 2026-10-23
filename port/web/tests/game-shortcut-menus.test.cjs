/* Browser shortcut popup tests: pointer routing and synthesized SDL key chords. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {installGameShortcutMenus} = require('../site/game-shortcut-menus.js');

function eventTarget() {
  const listeners = new Map();
  return {
    addEventListener(name, cb) {
      if (!listeners.has(name)) listeners.set(name, []);
      listeners.get(name).push(cb);
    },
    fire(name, event = {}) {
      for (const cb of listeners.get(name) || []) cb(event);
    }
  };
}
function button(attributes = {}) {
  const node = eventTarget();
  node.attributes = {...attributes};
  node.getAttribute = name => node.attributes[name] || null;
  node.setAttribute = (name, value) => { node.attributes[name] = value; };
  node.classList = {
    values: new Set(),
    add(name) { this.values.add(name); },
    remove(name) { this.values.delete(name); },
    contains(name) { return this.values.has(name); }
  };
  node.closest = selector => selector === 'button[data-game-key]' &&
    node.attributes['data-game-key'] ? node : null;
  node.focus = () => {};
  return node;
}
function fixture() {
  const doc = eventTarget(), win = eventTarget(), canvas = eventTarget();
  const html = button();
  doc.documentElement = html;
  const timers = new Map();
  let nextTimer = 1;
  win.setTimeout = callback => {const id = nextTimer++; timers.set(id, callback); return id;};
  win.clearTimeout = id => timers.delete(id);
  const emitted = [];
  let focusCount = 0, hit = null;
  win.KeyboardEvent = class {
    constructor(type, properties) {
      this.type = type;
      Object.assign(this, properties);
      // Simulate browsers that ignore legacy keyCode/which in the constructor.
      this.keyCode = 0;
      this.which = 0;
    }
  };
  canvas.focus = () => { focusCount++; };
  canvas.dispatchEvent = e => emitted.push(e);
  doc.elementFromPoint = () => hit;
  const launchGame = button(), launchGroups = button();
  const game = Object.assign(eventTarget(), {hidden:true});
  const groups = Object.assign(eventTarget(), {hidden:true});
  const quests = button({'data-game-key':'F9'});
  const chat = button({'data-game-key':'F12'});
  const plain = button({'data-game-key':'1'});
  const ctrl = button({'data-game-key':'1', 'data-game-modifier':'Control'});
  const shift = button({'data-game-key':'9', 'data-game-modifier':'Shift'});
  game.contains = item => [quests,chat].includes(item);
  groups.contains = item => [plain,ctrl,shift].includes(item);
  game.querySelector = () => quests;
  groups.querySelector = () => plain;
  installGameShortcutMenus(win, doc, canvas, [
    [launchGame, game], [launchGroups, groups]
  ]);
  const pointer = (type, pointerId = 4) => ({
    type, pointerId, button:0, isPrimary:true, clientX:120, clientY:90,
    cancelable:true, prevented:false, stopped:false,
    preventDefault() { this.prevented = true; },
    stopPropagation() { this.stopped = true; }
  });
  function drag(launch, item, id = 4) {
    launch.fire('pointerdown', pointer('pointerdown', id));
    hit = item;
    doc.fire('pointermove', pointer('pointermove', id));
    doc.fire('pointerup', pointer('pointerup', id));
  }
  return {win,doc,canvas,emitted,drag,pointer,launchGame,launchGroups,game,groups,
    quests,chat,plain,ctrl,shift,
    setHit(value) {hit = value;}, get focusCount() {return focusCount;},
    flushTimers() {const pending = [...timers.values()]; timers.clear(); pending.forEach(fn => fn());}};
}
function sequence(events) {
  return events.map(e => [e.type, e.key, e.code, e.keyCode, e.which, e.ctrlKey, e.shiftKey]);
}

test('game popup opens on finger down; releasing an F-key dispatches exactly one tap', () => {
  const f=fixture();
  const down=f.pointer('pointerdown');
  f.launchGame.fire('pointerdown', down);
  assert.equal(down.prevented, true);
  assert.equal(f.game.hidden, false);
  assert.equal(f.launchGame.getAttribute('aria-expanded'), 'true');
  assert.deepEqual(f.emitted, []);
  f.setHit(f.chat);
  f.doc.fire('pointermove', f.pointer('pointermove'));
  assert.equal(f.chat.classList.contains('is-target'), true);
  const up=f.pointer('pointerup');
  f.doc.fire('pointerup', up);
  assert.equal(up.stopped, true);
  assert.equal(f.game.hidden, true);
  assert.equal(f.launchGame.getAttribute('aria-expanded'), 'false');
  assert.equal(f.chat.classList.contains('is-target'), false);
  assert.deepEqual(sequence(f.emitted), [
    ['keydown','F12','F12',123,123,false,false],
    ['keyup','F12','F12',123,123,false,false]
  ]);
  assert.equal(f.focusCount, 1);
});

test('plain group recall, Ctrl assign and Shift add generate balanced key chords', () => {
  const f=fixture();
  f.drag(f.launchGroups, f.plain);
  assert.deepEqual(sequence(f.emitted.splice(0)), [
    ['keydown','1','Digit1',49,49,false,false],
    ['keyup','1','Digit1',49,49,false,false]
  ]);
  f.drag(f.launchGroups, f.ctrl);
  assert.deepEqual(sequence(f.emitted.splice(0)), [
    ['keydown','Control','ControlLeft',17,17,true,false],
    ['keydown','1','Digit1',49,49,true,false],
    ['keyup','1','Digit1',49,49,true,false],
    ['keyup','Control','ControlLeft',17,17,false,false]
  ]);
  f.drag(f.launchGroups, f.shift);
  assert.deepEqual(sequence(f.emitted.splice(0)), [
    ['keydown','Shift','ShiftLeft',16,16,false,true],
    ['keydown','9','Digit9',57,57,false,true],
    ['keyup','9','Digit9',57,57,false,true],
    ['keyup','Shift','ShiftLeft',16,16,false,false]
  ]);
});

test('release outside, pointercancel and focus loss close without sending a key', () => {
  const f=fixture();
  f.drag(f.launchGroups, null);
  assert.equal(f.groups.hidden, true);
  f.launchGame.fire('pointerdown', f.pointer('pointerdown'));
  f.setHit(f.quests);
  f.doc.fire('pointercancel', f.pointer('pointercancel'));
  assert.equal(f.game.hidden, true);
  f.launchGame.fire('pointerdown', f.pointer('pointerdown'));
  f.win.fire('blur');
  assert.equal(f.game.hidden, true);
  f.launchGame.fire('pointerdown', f.pointer('pointerdown'));
  f.doc.hidden = true;
  f.doc.fire('visibilitychange');
  assert.equal(f.game.hidden, true);
  assert.equal(f.emitted.length, 0);
});

test('unrelated pointer release cannot activate a popup or interfere with its drag', () => {
  const f=fixture();
  f.launchGroups.fire('pointerdown', f.pointer('pointerdown', 4));
  f.setHit(f.ctrl);
  f.doc.fire('pointerup', f.pointer('pointerup', 5));
  assert.equal(f.groups.hidden, false);
  assert.equal(f.emitted.length, 0);
  f.doc.fire('pointerup', f.pointer('pointerup', 4));
  assert.equal(f.groups.hidden, true);
  assert.equal(f.emitted.length, 4);
});

test('keyboard activation and Escape dismissal use the same shortcut path', () => {
  const f=fixture();
  const enter = {key:'Enter', preventDefault() {}};
  f.launchGame.fire('keydown', enter);
  assert.equal(f.game.hidden, false);
  f.game.fire('keydown', {key:'Escape', preventDefault() {}});
  assert.equal(f.game.hidden, true);
  f.launchGame.fire('keydown', enter);
  f.game.fire('keydown', {key:'Enter', target:f.quests, preventDefault() {}});
  assert.deepEqual(f.emitted.map(e => e.code), ['F9','F9']);
});

test('keyboard-open popup supports pointer selection and outside dismissal', () => {
  const f = fixture();
  const open = () => f.launchGame.fire('keydown', {key:'Enter', preventDefault() {}});
  open();
  f.setHit(f.quests);
  f.doc.fire('pointerdown', {...f.pointer('pointerdown'), target:f.quests});
  f.doc.fire('pointerup', f.pointer('pointerup'));
  assert.equal(f.game.hidden, true);
  assert.deepEqual(f.emitted.map(e => e.code), ['F9', 'F9']);

  f.emitted.length = 0;
  open();
  f.setHit(null);
  f.doc.fire('pointerdown', {...f.pointer('pointerdown'), target:f.canvas});
  f.doc.fire('pointerup', f.pointer('pointerup'));
  assert.equal(f.game.hidden, true);
  assert.deepEqual(f.emitted, []);
  f.drag(f.launchGroups, f.plain);
  assert.deepEqual(f.emitted.map(e => e.code), ['Digit1', 'Digit1']);
});

test('tabbing out closes a keyboard popup without blocking another launcher', () => {
  const f = fixture();
  f.launchGame.fire('keydown', {key:'Enter', preventDefault() {}});
  f.doc.fire('focusin', {target:f.quests});
  assert.equal(f.game.hidden, false);
  f.doc.fire('focusin', {target:f.launchGroups});
  assert.equal(f.game.hidden, true);
  assert.deepEqual(f.emitted, []);
  f.drag(f.launchGroups, f.ctrl);
  assert.deepEqual(f.emitted.map(e => e.code), ['ControlLeft', 'Digit1', 'Digit1', 'ControlLeft']);
});


test('touch drag prevents Safari selection and callouts across the page only while dragging', () => {
  const f = fixture();
  const selecting = () => ({cancelable:true, prevented:false,
    preventDefault() {this.prevented = true;}});
  let selection = selecting();
  f.doc.fire('selectstart', selection);
  assert.equal(selection.prevented, false);

  f.launchGame.fire('pointerdown', f.pointer('pointerdown'));
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), true);
  for (const name of ['touchstart', 'touchmove', 'selectstart', 'contextmenu', 'dragstart']) {
    const event = selecting();
    f.doc.fire(name, event);
    assert.equal(event.prevented, true, name);
  }
  f.setHit(null);
  f.doc.fire('pointerup', f.pointer('pointerup'));
  assert.equal(f.game.hidden, true);
  assert.equal(f.emitted.length, 0);
  // Safari may start selecting shortly after touchup; retain protection briefly.
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), true);
  f.flushTimers();
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), false);
  selection = selecting();
  f.doc.fire('selectstart', selection);
  assert.equal(selection.prevented, false);
});

test('an immediate second drag keeps the selection lock until the second release', () => {
  const f = fixture();
  f.launchGroups.fire('pointerdown', f.pointer('pointerdown'));
  f.doc.fire('pointercancel', f.pointer('pointercancel'));
  f.launchGame.fire('pointerdown', f.pointer('pointerdown'));
  f.flushTimers();
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), true);
  f.doc.fire('pointerup', f.pointer('pointerup'));
  f.flushTimers();
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), false);
});

test('keyboard-only popup allows selection; losing window focus immediately unlocks a drag', () => {
  const f = fixture();
  f.launchGame.fire('keydown', {key:'Enter', preventDefault() {}});
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), false);
  const select = {cancelable:true, prevented:false,
    preventDefault() {this.prevented = true;}};
  f.doc.fire('selectstart', select);
  assert.equal(select.prevented, false);
  f.win.fire('blur');
  f.launchGroups.fire('pointerdown', f.pointer('pointerdown'));
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), true);
  f.win.fire('blur');
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), false);
  assert.equal(f.groups.hidden, true);
  f.flushTimers();
  assert.equal(f.doc.documentElement.classList.contains('game-shortcut-dragging'), false);
  assert.equal(f.emitted.length, 0);
});
