/* No network/retail archives: Service Worker isolation and startup gate. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {pageMatches, ensurePagesIsolation, waitForActivation} = require('../site/pages-isolation.js');
const repo = path.resolve(__dirname, '../../..');

function windowStub(pathname) {
  const store = new Map();
  const listeners = new Map();
  const swURL = 'https://corepunch.github.io/openwarcraft3/play/coi-serviceworker.js';
  const serviceWorker = {
    controller: {scriptURL: swURL},
    ready: Promise.resolve(),
    register: async (url, options) => {
      assert.equal(url, swURL);
      assert.deepEqual(options, {scope: '/openwarcraft3/play/'});
      return {active: {scriptURL: swURL}};
    },
    addEventListener: (type, callback) => listeners.set(type, callback),
    removeEventListener: type => listeners.delete(type)
  };
  const status = {textContent: ''};
  const win = {
    location: {href: 'https://corepunch.github.io' + pathname, pathname,
      reload() {win.reloadCount++;}},
    isSecureContext: true, crossOriginIsolated: false,
    sessionStorage: {getItem:key => store.get(key), setItem:(key,value) => store.set(key,value), removeItem:key => store.delete(key)},
    document: {getElementById:id => id === 'status' ? status : null},
    navigator: {serviceWorker}, reloadCount: 0
  };
  return {win, status, serviceWorker, store};
}

test('Pages registration is limited to the game directory', async () => {
  assert.equal(pageMatches('/openwarcraft3/play/'), true);
  assert.equal(pageMatches('/openwarcraft3/play/index.html'), true);
  assert.equal(pageMatches('/openwarcraft3/'), false);
  assert.equal(pageMatches('/openwarcraft3/docs/'), false);
  const {win} = windowStub('/openwarcraft3/docs/');
  win.navigator.serviceWorker.register = () => {throw Error('docs must not register worker');};
  assert.equal(await ensurePagesIsolation(win), true);
});

test('first visit reloads once after control, but never loops', async () => {
  const {win, store, status} = windowStub('/openwarcraft3/play/');
  assert.equal(await ensurePagesIsolation(win), false);
  assert.equal(win.reloadCount, 1);
  assert.match(status.textContent, /Enabling browser isolation/);
  assert.equal(await ensurePagesIsolation(win), false);
  assert.equal(win.reloadCount, 1);
  assert.match(status.textContent, /unavailable after reload/);
  win.crossOriginIsolated = true;
  assert.equal(await ensurePagesIsolation(win), true);
  assert.equal(store.size, 0);
});

test('unsupported Service Worker fails without attempting the engine', async () => {
  const {win, status} = windowStub('/openwarcraft3/play/');
  delete win.navigator.serviceWorker;
  assert.equal(await ensurePagesIsolation(win), false);
  assert.match(status.textContent, /Service Worker support/);
  assert.equal(win.reloadCount, 0);
});

test('a Service Worker that never activates times out instead of holding startup', async () => {
  assert.equal(await waitForActivation({ready:new Promise(() => {})}, 1), false);
});

test('worker rewrites only same-origin game resources; no docs interception or cache', async () => {
  const callbacks = new Map();
  const worker = {
    location: {origin: 'https://corepunch.github.io'},
    addEventListener: (type, handler) => callbacks.set(type, handler),
    skipWaiting: () => Promise.resolve(),
    clients: {claim: () => Promise.resolve()}
  };
  const source = fs.readFileSync(path.join(repo, 'port/web/site/coi-serviceworker.js'), 'utf8');
  const context = {self:worker, URL, Headers, Response, fetch: async () => new Response('ok')};
  vm.runInNewContext(source, context);
  let responsePromise;
  const nav = {request: {method:'GET', url:'https://corepunch.github.io/openwarcraft3/play/index.html'}, respondWith:p => responsePromise = p};
  callbacks.get('fetch')(nav);
  const response = await responsePromise;
  assert.equal(response.headers.get('cross-origin-opener-policy'), 'same-origin');
  assert.equal(response.headers.get('cross-origin-embedder-policy'), 'require-corp');
  assert.equal(response.headers.get('cross-origin-resource-policy'), 'same-origin');
  responsePromise = null;
  callbacks.get('fetch')({request:{method:'GET',url:'https://corepunch.github.io/openwarcraft3/index.html'}, respondWith:p => responsePromise=p});
  assert.equal(responsePromise, null);
  callbacks.get('fetch')({request:{method:'GET',url:'https://example.org/openwarcraft3/play/index.html'}, respondWith:p => responsePromise=p});
  assert.equal(responsePromise, null);
  assert.doesNotMatch(source, /caches\.open\(/);
});

test('boot script loads before filesystem launcher and gates engine startup', () => {
  const shell = fs.readFileSync(path.join(repo, 'port/web/site/engine-shell.html'), 'utf8');
  assert.ok(shell.indexOf('src="pages-isolation.js"') < shell.indexOf('src="filesystem-bridge.js"'));
  assert.match(shell, /return window\.openrealmPagesReady\.then/);
  assert.match(shell, /var browserCapable =/);
  assert.match(shell, /return ready \? openrealmStartIsolatedEngine\(arguments_\) : false/);
  assert.match(shell, /if \(!window\.crossOriginIsolated\)/);
  const build = fs.readFileSync(path.join(repo, 'port/web/build-engine.py'), 'utf8');
  assert.match(build, /site\/pages-isolation\.js/);
  const workflow = fs.readFileSync(path.join(repo, '.github/workflows/docs.yml'), 'utf8');
  assert.match(workflow, /port\/web\/publish-pages\.py build\/web\/engine site\/play/);
  assert.doesNotMatch(fs.readFileSync(path.join(repo, 'mkdocs.yml'), 'utf8'), /Play Warcraft III/);
});
