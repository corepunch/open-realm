/* Loading handoff regression: placeholder must not reveal stale menu frames. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {makeEarlyMapLoading} = require('../site/early-map-loading.js');

test('placeholder stays visible until browser painting opportunities', () => {
  const frames = [];
  const old = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = fn => { frames.push(fn); };
  try {
    const node = {hidden:true};
    const show = makeEarlyMapLoading({getElementById:()=>node});
    show(true);
    show(false);
    assert.equal(node.hidden, false);
    frames.shift()();
    assert.equal(node.hidden, false);
    frames.shift()();
    assert.equal(node.hidden, true);
  } finally { globalThis.requestAnimationFrame = old; }
});

test('a new map transition cancels an earlier pending hide', () => {
  const frames = [];
  const old = globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame = fn => { frames.push(fn); };
  try {
    const node = {hidden:true};
    const show = makeEarlyMapLoading({getElementById:()=>node});
    show(true); show(false); show(true);
    frames.shift()(); frames.shift()();
    assert.equal(node.hidden, false);
  } finally { globalThis.requestAnimationFrame = old; }
});

test('native handoff is owned by a submitted authored loading frame', () => {
  const base = path.resolve(__dirname, '../../..');
  const prep = fs.readFileSync(path.join(base, 'client/cl_configstrings.c'), 'utf8');
  const screen = fs.readFileSync(path.join(base, 'client/cl_scrn.c'), 'utf8');
  const prepBody = prep.split('void CL_PrepLoading(void) {')[1].split('}', 1)[0];
  assert.doesNotMatch(prepBody, /Web_LoadingOverlay\(false\)/);
  assert.match(screen, /SCR_DrawScreenField\(0\);\s*#ifdef __EMSCRIPTEN__[\s\S]*?Web_LoadingOverlay\(false\)/);
});
