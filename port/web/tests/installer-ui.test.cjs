/* Real installer.js event handlers with a minimal DOM/Worker harness; no game data. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const SITE = path.resolve(__dirname, '../site');

class Element {
  constructor(id) {
    this.id = id;
    this.textContent = '';
    this.value = id === 'install-progress' ? 0 : '';
    this.disabled = false;
    this.hidden = id === 'discovered-files';
    this.files = [];
    this.children = [];
    this.listeners = new Map();
  }
  addEventListener(name, callback) { this.listeners.set(name, callback); }
  fire(name) { this.listeners.get(name)(); }
  replaceChildren() { this.children = []; }
  appendChild(node) { this.children.push(node); }
  removeAttribute(name) { if (name === 'value') this.value = undefined; }
}
function harness({storage = true} = {}) {
  const ids = ['game-files', 'install-data', 'cancel-install', 'remove-data',
    'data-status', 'install-progress', 'selection-summary', 'discovered-files', 'archive-list',
    'installation-edition'];
  const elements = Object.fromEntries(ids.map(id => [id, new Element(id)]));
  let worker;
  class Worker {
    constructor(url) {
      assert.equal(url, 'install-worker.js');
      worker = this;
      this.messages = [];
    }
    postMessage(message) { this.messages.push(message); }
    send(message) { this.onmessage({data:message}); }
  }
  const context = vm.createContext({
    document: {
      getElementById: id => elements[id],
      createElement: tag => {
        assert.equal(tag, 'li');
        return new Element('li');
      }
    },
    window: {Worker}, Worker,
    navigator: {storage: storage ? {getDirectory: () => {}} : undefined,
      locks: {request:async (name, options, callback) => callback({name})}},
    confirm: () => true
  });
  vm.runInContext(fs.readFileSync(path.join(SITE, 'installer.js'), 'utf8'),
    context, {filename:'installer.js'});
  return {elements, worker};
}

test('both browser pages expose ISO/mixed selection and detected archive list', () => {
  for (const name of ['shell.html', 'engine-shell.html']) {
    const html = fs.readFileSync(path.join(SITE, name), 'utf8');
    assert.match(html, /id="game-files"[^>]*accept="\.iso,\.mpq,\.w3m,\.w3x"[^>]*multiple/);
    for (const id of ['selection-summary', 'discovered-files', 'archive-list', 'data-status', 'installation-edition'])
      assert.ok(html.includes(`id="${id}"`), `${name}: missing ${id}`);
    assert.match(html, /<script src="installer\.js"><\/script>/);
  }
});

test('multi-disc selection, validated discovery, copying and final UI states', () => {
  const {elements: e, worker} = harness();
  assert.deepEqual(Array.from(worker.messages, message => message.type), ['list']);
  e['game-files'].files = [{name:'RoC.iso'}, {name:'TFT.iso'}, {name:'War3Patch.mpq'}];
  e['game-files'].fire('change');
  assert.match(e['selection-summary'].textContent, /2 ISO image\(s\) and 1 standalone/);
  e['install-data'].fire('click');
  assert.equal(worker.messages.at(-1).type, 'import');
  assert.equal(worker.messages.at(-1).files.length, 3);
  assert.equal(e['install-progress'].value, undefined);
  assert.equal(e['install-data'].disabled, true);
  assert.equal(e['cancel-install'].disabled, false);
  assert.equal(e['game-files'].disabled, true);
  worker.send({type:'list', manifest:null}); // Delayed initial response is ignored.
  assert.match(e['data-status'].textContent, /Inspecting/);
  worker.send({type:'phase', phase:'scanning', source:'RoC.iso'});
  assert.match(e['data-status'].textContent, /RoC.iso/);
  worker.send({type:'discovered', bytes:128, files:[
    {name:'War3.mpq', size:64, source:'RoC.iso'},
    {name:'War3x.mpq', size:64, source:'TFT.iso'}
  ]});
  assert.equal(e['discovered-files'].hidden, false);
  assert.deepEqual(e['archive-list'].children.map(child => child.textContent),
    ['War3.mpq (64 B) — RoC.iso', 'War3x.mpq (64 B) — TFT.iso']);
  worker.send({type:'progress', file:'War3.mpq', copied:64, total:128, index:1, count:2});
  assert.equal(e['install-progress'].value, 0.5);
  assert.match(e['data-status'].textContent, /1\/2/);
  worker.send({type:'phase', phase:'committing'});
  assert.match(e['data-status'].textContent, /manifest/);
  worker.send({type:'done', manifest:{bytes:128, files:[{name:'War3.mpq'}, {name:'War3x.mpq'}]}});
  assert.equal(e['install-progress'].value, 1);
  assert.equal(e['install-data'].disabled, false);
  assert.equal(e['cancel-install'].disabled, true);
  assert.match(e['data-status'].textContent, /Reload this page/);
});

test('cancellation, failed import and removal do not display stale discoveries', () => {
  const {elements: e, worker} = harness();
  e['game-files'].files = [{name:'RoC.iso'}];
  e['install-data'].fire('click');
  worker.send({type:'discovered', bytes:64,
    files:[{name:'War3.mpq', size:64, source:'RoC.iso'}]});
  e['cancel-install'].fire('click');
  assert.equal(worker.messages.at(-1).type, 'cancel');
  assert.equal(e['cancel-install'].disabled, true);
  worker.send({type:'error', message:'Import cancelled'});
  assert.match(e['data-status'].textContent, /cancelled/);
  e['install-data'].fire('click');
  assert.equal(e['discovered-files'].hidden, true);
  worker.send({type:'error', message:'Duplicate archive War3.mpq'});
  assert.match(e['data-status'].textContent, /Duplicate archive/);
  e['remove-data'].fire('click');
  assert.equal(worker.messages.at(-1).type, 'remove');
  assert.equal(e['remove-data'].disabled, true);
  worker.send({type:'list', manifest:null});
  assert.equal(e['discovered-files'].hidden, true);
  assert.equal(e['remove-data'].disabled, false);
  assert.equal(e['data-status'].textContent, 'No game data installed.');
});

test('missing OPFS disables installer controls', () => {
  const {elements: e, worker} = harness({storage:false});
  assert.equal(worker, undefined);
  assert.equal(e['install-data'].disabled, true);
  assert.equal(e['remove-data'].disabled, true);
  assert.equal(e['game-files'].disabled, true);
});

test('both browser build targets ship all referenced installer scripts', () => {
  const root = path.resolve(SITE, '..');
  const foundation = fs.readFileSync(path.join(root, 'build.mk'), 'utf8');
  const engine = fs.readFileSync(path.join(root, 'build-engine.py'), 'utf8');
  for (const asset of ['installer.js', 'install-worker.js', 'iso9660.js', 'setup-mpq.js', 'mpq-header.js']) {
    assert.ok(foundation.includes(`port/web/site/${asset}`), `foundation build missing ${asset}`);
    assert.ok(engine.includes(`"${asset}"`), `engine build missing ${asset}`);
  }
});

test('unknown installer layout warning remains visible after committed import', () => {
  const {elements:e, worker} = harness();
  e['game-files'].files = [{name:'RoC.iso'}];
  e['install-data'].fire('click');
  worker.send({type:'phase', phase:'extracting'});
  assert.match(e['data-status'].textContent, /Setup.mpq/);
  worker.send({type:'notice', message:'Unknown Setup.mpq layout in RoC.iso; maps skipped.'});
  worker.send({type:'phase', phase:'copying'});
  worker.send({type:'done', manifest:{files:[{name:'War3.mpq',size:64}],bytes:64}});
  assert.match(e['data-status'].textContent, /Unknown Setup.mpq layout/);
  assert.equal(e['install-data'].disabled, false);
});

test('partial expansion is distinguished from complete installation in UI', () => {
  const {elements:e, worker} = harness();
  e['game-files'].files = [{name:'RoC.iso'}, {name:'TFT.iso'}];
  e['install-data'].fire('click');
  worker.send({type:'done', manifest:{completeness:'partial-expansion',
    bytes:128, files:[{name:'War3.mpq'}, {name:'War3x.mpq'}]}});
  assert.match(e['data-status'].textContent, /Partial Frozen Throne installation/);
});

test('repository ignore rules retain browser source modules as committable files', () => {
  const ignore = fs.readFileSync(path.resolve(SITE, '../../..', '.gitignore'), 'utf8');
  assert.match(ignore, /^\/site\/$/m);
  assert.doesNotMatch(ignore, /^site\/$/m);
});


test('demo edition is clearly labelled on import, reload and removal', () => {
  const {elements:e, worker} = harness();
  e['game-files'].files = [{name:'2002.iso'}];
  e['install-data'].fire('click');
  worker.send({type:'done', manifest:{edition:'demo', completeness:'demo', bytes:64,
    files:[{name:'War3.mpq', size:64}]}});
  assert.match(e['data-status'].textContent, /Warcraft III Demo installed/);
  assert.equal(e['installation-edition'].hidden, false);
  assert.match(e['installation-edition'].textContent, /limited demo content/);
  worker.send({type:'list', manifest:{edition:'demo', completeness:'demo',
    files:[{name:'War3.mpq'}]}});
  assert.equal(e['installation-edition'].hidden, false);
  assert.match(e['data-status'].textContent, /Demo installed/);
  worker.send({type:'list', manifest:null});
  assert.equal(e['installation-edition'].hidden, true);
});
