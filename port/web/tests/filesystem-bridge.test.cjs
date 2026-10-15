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
const mpq = map.subarray(0, 64);
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
async function launch(mapPath) {
  const manifest = {
    version:3, generation:'install-abcd-1234',
    files:[{name:'War3.mpq', size:mpq.length}, {name:mapPath, size:map.length}],
    bytes:mpq.length + map.length
  };
  const stage = dir({'War3.mpq':new Blob([mpq]), 'Maps':dir({
    '(4)LostTemple.w3m':new Blob([map])
  })});
  const store = dir({openrealm:dir({'game-data':dir({wc3:dir({
    'installation.json':new Blob([JSON.stringify(manifest)]),
    'install-abcd-1234':stage
  })})})});
  const starts = [];
  const status = {textContent:''};
  const context = vm.createContext({
    navigator:{storage:{getDirectory:async () => store}},
    document:{getElementById:() => status},
    window:{openrealmStartEngine:args => starts.push(args)},
    Uint8Array, Number
  });
  vm.runInContext(fs.readFileSync(path.join(site, 'filesystem-bridge.js'), 'utf8'), context);
  await new Promise(resolve => setImmediate(resolve));
  return {starts, status};
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
