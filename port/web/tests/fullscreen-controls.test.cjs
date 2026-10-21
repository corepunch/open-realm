/* Chrome and Safari fullscreen API variants, without browser/game assets. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {installFullscreenControls} = require('../site/fullscreen-controls.js');

function target() {
  const listeners = new Map();
  return {
    addEventListener(name, callback) {
      if (!listeners.has(name)) listeners.set(name, []);
      listeners.get(name).push(callback);
    },
    fire(name) {
      for (const callback of listeners.get(name) || [])
        callback({stopPropagation() {}});
    }
  };
}
function fixture() {
  const doc=target(), button=target(), container=target(), canvas=target();
  const win=target(), status={textContent:''};
  const styles=new Set(), attrs=new Map(), notifications=[];
  container.classList={toggle(name, enabled) {
    if (enabled) styles.add(name);
    else styles.delete(name);
  }};
  button.setAttribute=(name, value)=>attrs.set(name, value);
  win.openrealmRequestFullscreenResize=()=>notifications.push('resize');
  doc.fullscreenElement=null;
  doc.webkitFullscreenElement=null;
  installFullscreenControls(win,doc,container,canvas,button,status);
  return {doc,button,container,canvas,win,status,styles,attrs,notifications};
}

test('standard Fullscreen API enters/exits and keeps its Promise result for Emscripten', async () => {
  // Install methods before wiring up the DOM, as browser APIs are on prototypes.
  const f=fixture();
  const requested=[];
  f.container.requestFullscreen=() => { requested.push('enter'); return Promise.resolve(); };
  f.doc.exitFullscreen=() => { requested.push('exit'); return Promise.resolve(); };
  f.button.fire('click');
  await Promise.resolve();
  f.doc.fullscreenElement=f.container;
  f.doc.fire('fullscreenchange');
  assert.deepEqual(requested,['enter']);
  assert.equal(f.styles.has('is-fullscreen'), true);
  assert.equal(f.attrs.get('aria-label'),'Exit fullscreen');
  f.button.fire('click');
  assert.deepEqual(requested,['enter','exit']);
  assert.deepEqual(f.notifications,['resize']);
});

test('Safari legacy WebKit fullscreen handles void returns and prefixed event', () => {
  const f=fixture();
  const requested=[];
  f.container.webkitRequestFullscreen=()=>{requested.push('enter');};
  f.doc.webkitExitFullscreen=()=>{requested.push('exit');};
  f.button.fire('click'); // A void return must not cause a TypeError on .catch.
  f.doc.webkitFullscreenElement=f.container;
  f.doc.fire('webkitfullscreenchange');
  assert.deepEqual(requested,['enter']);
  assert.equal(f.styles.has('is-fullscreen'),true);
  assert.equal(f.attrs.get('aria-label'),'Exit fullscreen');
  f.button.fire('click');
  f.doc.webkitFullscreenElement=null;
  f.doc.fire('webkitfullscreenchange');
  assert.deepEqual(requested,['enter','exit']);
  assert.equal(f.styles.has('is-fullscreen'),false);
  assert.equal(f.attrs.get('aria-label'),'Enter fullscreen');
  assert.deepEqual(f.notifications,['resize','resize']);
});

test('canvas requests target container and preserve native Promise', async () => {
  const f=fixture();
  let calls=0;
  // Reinstall with a native fullscreen method exposed on canvas.
  f.container.requestFullscreen=()=>{calls++;return Promise.resolve('entered');};
  f.canvas.requestFullscreen=()=>Promise.reject(new Error('wrong target'));
  installFullscreenControls(f.win,f.doc,f.container,f.canvas,f.button,f.status);
  assert.equal(await f.canvas.requestFullscreen(),'entered');
  assert.equal(calls,1);
});


test('Safari canvas requests redirect through prefixed API without assuming a Promise', () => {
  const f=fixture();
  let calls=0;
  f.container.webkitRequestFullscreen=()=>{calls++;};
  f.canvas.webkitRequestFullscreen=()=>{throw new Error('wrong target');};
  installFullscreenControls(f.win,f.doc,f.container,f.canvas,f.button,f.status);
  assert.equal(f.canvas.webkitRequestFullscreen(),undefined);
  assert.equal(calls,1);
  assert.equal(f.status.textContent,'');
});

test('request rejection and missing API show useful errors', async () => {
  const f=fixture();
  f.button.fire('click');
  assert.match(f.status.textContent,/Fullscreen unavailable/);
  f.container.requestFullscreen=()=>Promise.reject(new Error('Permission denied'));
  f.button.fire('click');
  await new Promise(resolve => setImmediate(resolve));
  assert.match(f.status.textContent,/Permission denied/);
});
