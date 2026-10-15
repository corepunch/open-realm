/* Verify browser launcher manifest validation with synthetic OPFS entries. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const site = path.resolve(__dirname, '../site');
const map = Buffer.alloc(256);
map.write('MPQ', 0); map[3] = 0x1a;
map.writeUInt32LE(32, 4); map.writeUInt32LE(256, 8);
map.writeUInt32LE(32, 16); map.writeUInt32LE(48, 20);
map.writeUInt32LE(1, 24); map.writeUInt32LE(1, 28);
const mpq = Buffer.from(map.subarray(0, 64));
mpq.writeUInt32LE(64, 8);
function dir(entries) {
  return {
    async getDirectoryHandle(name) {
      const value = entries[name];
      if (!value || typeof value.getDirectoryHandle !== 'function') throw new Error('Missing directory ' + name);
      return value;
    },
    async getFileHandle(name) {
      const value = entries[name];
      if (!(value instanceof Blob)) throw new Error('Missing file ' + name);
      return {getFile: async () => value};
    }
  };
}
async function launch(mapPath, {corruptBase = false, edition, completeness, expansion = false, requested = null} = {}) {
  const manifest = {
    version:3, generation:'install-abcd-1234',
    ...(edition ? {edition, completeness} : {}),
    files:[{name:'War3.mpq', size:mpq.length}, {name:mapPath, size:map.length},
      ...(expansion ? [{name:'War3x.mpq',size:mpq.length}] : [])],
    bytes:mpq.length + map.length + (expansion ? mpq.length : 0)
  };
  const broken = Buffer.from(mpq);
  if (corruptBase) broken.writeUInt32LE(1024, 8);
  const stage = dir({'War3.mpq':new Blob([corruptBase ? broken : mpq]),
    ...(expansion ? {'War3x.mpq':new Blob([mpq])} : {}), 'Maps':dir({
    '(4)LostTemple.w3m':new Blob([map])
  })});
  const store = dir({openrealm:dir({'game-data':dir({wc3:dir({
    'installation.json':new Blob([JSON.stringify(manifest)]),
    'install-abcd-1234':stage
  })})})});
  const starts = [];
  const lockRequests = [];
  const status = {textContent:''};
  const selector = {value:'roc',disabled:true,handlers:{},addEventListener(name,fn){this.handlers[name]=fn;}};
  const session = new Map(requested ? [['openrealm-wc3-browser-mode',requested]] : []);
  let reloads = 0;
  const context = vm.createContext({
    navigator:{storage:{getDirectory:async () => store}, locks:{
      request:async (name, options, callback) => {
        lockRequests.push([name, options.mode]);
        return callback({name, mode:options.mode});
      }
    }},
    document:{getElementById:id => id === 'game-edition' ? selector : status},
    sessionStorage:{getItem:key=>session.get(key) ?? null,setItem:(key,value)=>session.set(key,value)},
    location:{reload:()=>{reloads++;}},
    window:{openrealmStartEngine:args => starts.push(args), MPQHeader:require('../site/mpq-header.js')},
    Uint8Array, Number, DataView
  });
  vm.runInContext(fs.readFileSync(path.join(site, 'filesystem-bridge.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  return {starts, status, lockRequests, selector, session, get reloads(){return reloads;}};
}
test('launcher accepts a validated version-3 map under Maps/', async () => {
  const {starts, status} = await launch('Maps/(4)LostTemple.w3m');
  assert.equal(starts.length, 1);
  assert.equal(starts[0][0], '+openrealm-data=/opfs/openrealm/game-data/wc3/install-abcd-1234');
  assert.match(status.textContent, /Validated/);
});
test('launcher rejects path traversal in a version-3 manifest', async () => {
  const {starts, status} = await launch('Maps/../(4)LostTemple.w3m');
  assert.equal(starts.length, 0);
  assert.match(status.textContent, /Invalid archive manifest entry/);
});

test('launcher holds a shared commit lock and generation lease while starting', async () => {
  const {starts, lockRequests} = await launch('Maps/(4)LostTemple.w3m');
  assert.equal(starts.length, 1);
  assert.deepEqual(lockRequests, [
    ['openrealm-wc3-installation', 'shared'],
    ['openrealm-wc3-generation:install-abcd-1234', 'shared']
  ]);
});

test('launcher rejects corrupt MPQ declared length before engine startup', async () => {
  const {starts, status} = await launch('Maps/(4)LostTemple.w3m', {corruptBase:true});
  assert.equal(starts.length, 0);
  assert.match(status.textContent, /Invalid MPQ archive War3.mpq/);
});


test('launcher accepts and visibly identifies a stored demo edition', async () => {
  const {starts, status} = await launch('Maps/(4)LostTemple.w3m',
    {edition:'demo', completeness:'demo'});
  assert.equal(starts.length, 1);
  assert.match(status.textContent, /Warcraft III Demo installation/);
});

test('launcher refuses contradictory demo edition metadata', async () => {
  const {starts, status} = await launch('Maps/(4)LostTemple.w3m',
    {edition:'demo', completeness:'expansion'});
  assert.equal(starts.length, 0);
  assert.match(status.textContent, /Invalid demo installation manifest/);
});


test('RoC is the default even with expansion archives installed', async () => {
  const result = await launch('Maps/(4)LostTemple.w3m', {expansion:true});
  assert.equal(result.selector.disabled, false);
  assert.equal(result.selector.value, 'roc');
  assert.deepEqual(Array.from(result.starts[0].slice(-3)), ['+set','fs_expansion','0']);
});

test('TFT selection passes expansion mode on next page launch', async () => {
  const result = await launch('Maps/(4)LostTemple.w3m',
    {expansion:true, requested:'tft'});
  assert.equal(result.selector.value, 'tft');
  assert.deepEqual(Array.from(result.starts[0].slice(-3)), ['+set','fs_expansion','1']);
  result.selector.value = 'roc';
  result.selector.handlers.change();
  assert.equal(result.session.get('openrealm-wc3-browser-mode'), 'roc');
  assert.equal(result.reloads, 1);
});

test('TFT preference falls back safely for a base-only install', async () => {
  const result = await launch('Maps/(4)LostTemple.w3m', {requested:'tft'});
  assert.equal(result.selector.disabled, true);
  assert.equal(result.selector.value, 'roc');
  assert.deepEqual(Array.from(result.starts[0].slice(-3)), ['+set','fs_expansion','0']);
});
