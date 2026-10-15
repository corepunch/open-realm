/* Browser fullscreen layout stabilization tests (synthetic DOM, no game data). */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {installFullscreenResize} = require('../site/fullscreen-resize.js');
function fixture() {
  const listeners = new Map(), queue = [], events = [];
  let rect = {width:960,height:540};
  const container = {}, canvas = {getBoundingClientRect:()=>rect};
  const doc = {fullscreenElement:null, addEventListener(n, fn) {
    listeners.set('doc:' + n, fn);
  }};
  const observers = [];
  const win = {devicePixelRatio:1, ResizeObserver:class {
    constructor(fn) {observers.push(fn);}
    observe() {}
  }, Event:class {constructor(type) {this.type=type;}},
  addEventListener(n,fn) {listeners.set('win:' + n,fn);},
  requestAnimationFrame(fn) {queue.push(fn);},
  dispatchEvent(ev) {events.push(ev.type); listeners.get('win:' + ev.type)?.(ev);}};
  const sync = installFullscreenResize(win,doc,container,canvas);
  function frame() {const next=queue.splice(0);for(const fn of next)fn();}
  return {doc,win,container,canvas,events,queue,sync,frame,
    changeSize(width,height) {rect={width,height};observers.forEach(fn=>fn());},
    fullscreen() {doc.fullscreenElement=container;listeners.get('doc:fullscreenchange')();},
    exit() {doc.fullscreenElement=null;listeners.get('doc:fullscreenchange')();}};
}

test('fullscreen event is delayed until browser layout settles', () => {
  const f=fixture();
  f.fullscreen();
  assert.deepEqual(f.events, []);
  f.frame();
  f.changeSize(1920,1080);
  f.frame();
  assert.deepEqual(f.events, []); // transient 960x540 must not reach SDL
  f.frame(); f.frame();
  assert.deepEqual(f.events, ['resize']);
});

test('fullscreen and exit coalesce notifications; events cannot loop', () => {
  const f=fixture();
  f.fullscreen(); f.changeSize(1600,900);
  f.frame(); f.frame();
  assert.deepEqual(f.events, ['resize']);
  f.frame(); f.frame();
  assert.deepEqual(f.events, ['resize']);
  f.exit(); f.changeSize(960,540);
  f.frame(); f.frame();
  assert.deepEqual(f.events, ['resize','resize']);
});

test('resizing or changing DPR sends a new notification once', () => {
  const f=fixture();
  f.changeSize(1280,720); f.frame(); f.frame();
  assert.deepEqual(f.events,['resize']);
  f.changeSize(1280,720); f.frame(); f.frame();
  assert.deepEqual(f.events,['resize']);
  f.win.devicePixelRatio=2; f.win.dispatchEvent(new f.win.Event('resize'));
  f.frame(); f.frame();
  // First resize was from browser. A second is emitted to synchronize SDL.
  assert.deepEqual(f.events,['resize','resize','resize']);
});

test('a hidden canvas never requests a zero-sized SDL framebuffer', () => {
  const f=fixture();
  f.changeSize(0,0); f.fullscreen(); f.frame(); f.frame();
  assert.deepEqual(f.events,[]);
});

// No synthetic DOM event alone can verify real CSS geometry. This checks the
// regression-inducing style: forcing a constant 16:9 aspect for windowed play.
test('windowed game canvas follows viewport aspect and ignores SDL inline sizes', () => {
  const html = fs.readFileSync(path.join(__dirname, '../site/engine-shell.html'), 'utf8');
  const rule = html.match(/#game-container:not\(:fullscreen\) > canvas:not\(:fullscreen\) \{([^}]+)\}/);
  assert.ok(rule, 'windowed canvas must have a dedicated CSS sizing rule');
  assert.match(rule[1], /width:\s*100%\s*!important/);
  assert.match(rule[1], /height:\s*min\(65dvh,\s*75vw,\s*620px\)\s*!important/);
  assert.match(rule[1], /aspect-ratio:\s*auto\s*!important/);
  assert.doesNotMatch(html, /canvas\s*\{[^}]*aspect-ratio:\s*16\s*\/\s*9/);
  assert.match(html, /#game-container:fullscreen canvas \{[^}]*width:\s*100% !important/);
});
