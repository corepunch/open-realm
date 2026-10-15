/* Exercise the real browser installer worker with synthetic ISO/MPQ bytes only. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const SITE = path.resolve(__dirname, '../site');
const BLOCK = 2048;
const {setupMpq} = require('./setup-mpq-fixture.cjs');

function put16(bytes, pos, value) {
  bytes.writeUInt16LE(value, pos);
  bytes.writeUInt16BE(value, pos + 2);
}
function put32(bytes, pos, value) {
  bytes.writeUInt32LE(value, pos);
  bytes.writeUInt32BE(value, pos + 4);
}
function record(name, extent, size, directory = false) {
  const id = typeof name === 'number' ? Buffer.from([name]) : Buffer.from(name, 'ascii');
  const length = 33 + id.length + (id.length % 2 ? 0 : 1);
  const bytes = Buffer.alloc(length);
  bytes[0] = length;
  put32(bytes, 2, extent);
  put32(bytes, 10, size);
  bytes[25] = directory ? 2 : 0;
  put16(bytes, 28, 1);
  bytes[32] = id.length;
  id.copy(bytes, 33);
  return bytes;
}
function mpq(length = 64, fill = 0) {
  const bytes = Buffer.alloc(length, fill);
  bytes.write('MPQ', 0, 'ascii');
  bytes[3] = 0x1a;
  bytes.writeUInt32LE(32, 4);
  bytes.writeUInt32LE(Math.max(64, length), 8);
  bytes.writeUInt16LE(0, 12);
  bytes.writeUInt16LE(0, 14);
  bytes.writeUInt32LE(32, 16);
  bytes.writeUInt32LE(48, 20);
  bytes.writeUInt32LE(1, 24);
  bytes.writeUInt32LE(1, 28);
  return bytes;
}
function iso(entries, {duplicate = false, retailMaps = [], name = 'Disc.iso'} = {}) {
  const sizeInSectors = [...entries, ...retailMaps].reduce(
    (total, [, data]) => total + Math.ceil(data.length / BLOCK), 0);
  const sectors = Math.max(48, 24 + sizeInSectors + 2);
  const bytes = Buffer.alloc(BLOCK * sectors);
  for (const [sector, type] of [[16, 1], [17, 255]]) {
    const pos = sector * BLOCK;
    bytes[pos] = type;
    bytes.write('CD001', pos + 1, 'ascii');
    bytes[pos + 6] = 1;
    if (type === 255) continue;
    put32(bytes, pos + 80, sectors);
    put16(bytes, pos + 128, BLOCK);
    record(0, 20, BLOCK, true).copy(bytes, pos + 156);
  }
  const rootEntries = [record(0, 20, BLOCK, true), record(1, 20, BLOCK, true)];
  let sector = 24;
  for (const [name, data] of entries) {
    rootEntries.push(record(name + ';1', sector, data.length));
    data.copy(bytes, sector * BLOCK);
    sector += Math.ceil(data.length / BLOCK);
  }
  if (retailMaps.length) {
    rootEntries.push(record('RETAILMAPS', 22, BLOCK, true));
    const mapEntries = [record(0, 22, BLOCK, true), record(1, 20, BLOCK, true)];
    for (const [name, data] of retailMaps) {
      mapEntries.push(record(name + ';1', sector, data.length));
      data.copy(bytes, sector * BLOCK);
      sector += Math.ceil(data.length / BLOCK);
    }
    let pos = 22 * BLOCK;
    for (const entry of mapEntries) { entry.copy(bytes, pos); pos += entry.length; }
  }
  if (duplicate) {
    rootEntries.push(record('NESTED', 21, BLOCK, true));
    const nestedEntries = [record(0, 21, BLOCK, true), record(1, 20, BLOCK, true),
      record('WAR3.MPQ;1', 24, mpq().length)];
    let pos = 21 * BLOCK;
    for (const entry of nestedEntries) { entry.copy(bytes, pos); pos += entry.length; }
  }
  let pos = 20 * BLOCK;
  for (const entry of rootEntries) { entry.copy(bytes, pos); pos += entry.length; }
  return namedBlob(bytes, name);
}
function namedBlob(bytes, name) {
  const blob = new Blob([bytes]);
  Object.defineProperty(blob, 'name', {value: name});
  return blob;
}
function missing(name) {
  const error = new Error('Missing ' + name);
  error.name = 'NotFoundError';
  throw error;
}
class MemoryFile {
  constructor(name, failWritesFor) {
    this.name = name;
    this.failWritesFor = failWritesFor;
    this.value = new Blob([]);
  }
  async getFile() { return this.value; }
  async createWritable() {
    const buffers = [];
    const handle = this;
    return {
      async write(data) {
        if (handle.name === handle.failWritesFor) throw new Error('Simulated OPFS write failure');
        buffers.push(data);
      },
      async close() { handle.value = new Blob(buffers); },
      async abort() { buffers.length = 0; }
    };
  }
}
class MemoryDir {
  constructor(failWritesFor = null) {
    this.dirs = new Map();
    this.files = new Map();
    this.failWritesFor = failWritesFor;
  }
  async getDirectoryHandle(name, {create = false} = {}) {
    if (!this.dirs.has(name)) {
      if (!create) missing(name);
      this.dirs.set(name, new MemoryDir(this.failWritesFor));
    }
    return this.dirs.get(name);
  }
  async getFileHandle(name, {create = false} = {}) {
    if (!this.files.has(name)) {
      if (!create) missing(name);
      this.files.set(name, new MemoryFile(name, this.failWritesFor));
    }
    return this.files.get(name);
  }
  async removeEntry(name) {
    if (!this.dirs.delete(name) && !this.files.delete(name)) missing(name);
  }
  async *entries() {
    for (const [name, dir] of this.dirs) yield [name, {kind:'directory', ...dir}];
    for (const [name, file] of this.files) yield [name, {kind:'file', ...file}];
  }
}
class TestLocks {
  constructor() { this.held = new Map(); }
  async request(name, options, callback) {
    const mode = options.mode || 'exclusive';
    const entries = this.held.get(name) || [];
    const busy = entries.some(entry => entry.mode === 'exclusive' || mode === 'exclusive');
    if (busy && options.ifAvailable) return callback(null);
    if (busy) throw new Error('Test lock contention (unexpected wait)');
    const token = {mode};
    entries.push(token);
    this.held.set(name, entries);
    try { return await callback({name, mode}); }
    finally {
      const updated = this.held.get(name) || [];
      updated.splice(updated.indexOf(token), 1);
      if (!updated.length) this.held.delete(name);
    }
  }
  hold(name, mode = 'shared') {
    const entries = this.held.get(name) || [];
    const token = {mode};
    entries.push(token);
    this.held.set(name, entries);
    return () => {
      entries.splice(entries.indexOf(token), 1);
      if (!entries.length) this.held.delete(name);
    };
  }
}
function makeWorker({estimate, failWritesFor = null, locks = new TestLocks()} = {}) {
  const root = new MemoryDir(failWritesFor);
  const messages = [];
  const storage = {getDirectory: async () => root};
  if (estimate !== undefined) storage.estimate = async () => estimate;
  const context = vm.createContext({
    Blob, AbortController, DOMException, DecompressionStream, Uint8Array, DataView, Number, Date, Math,
    navigator: {storage, locks},
    postMessage: message => messages.push(message)
  });
  context.importScripts = (...scripts) => {
    for (const script of scripts) {
      assert.ok(['iso9660.js', 'setup-mpq.js', 'mpq-header.js'].includes(script));
      vm.runInContext(fs.readFileSync(path.join(SITE, script), 'utf8'), context,
        {filename:script});
    }
  };
  vm.runInContext(fs.readFileSync(path.join(SITE, 'install-worker.js'), 'utf8'),
    context, {filename:'install-worker.js'});
  async function run(files) {
    messages.length = 0;
    await context.onmessage({data: {type:'import', files}});
    return messages.slice();
  }
  async function gameDir() {
    return (await (await root.getDirectoryHandle('openrealm')).getDirectoryHandle('game-data'))
      .getDirectoryHandle('wc3');
  }
  return {run, messages, context, gameDir, locks};
}

test('ISO import copies only recognized archive extents and writes v3 manifest', async () => {
  const worker = makeWorker();
  const data = mpq();
  const output = await worker.run([iso([
    ['WAR3.MPQ', data], ['SETUP.MPQ', setupMpq({count:1})],
    ['WAR3LOCAL.MPQ', data], ['README.TXT', Buffer.from('ignored')]
  ])]);
  assert.equal(output.at(-1).type, 'done');
  const manifest = output.at(-1).manifest;
  assert.equal(manifest.version, 3);
  assert.deepEqual(Array.from(manifest.files, f => f.name), ['War3.mpq', 'War3Local.mpq']);
  assert.equal(manifest.bytes, 2 * data.length);
  assert.ok(output.some(m => m.type === 'progress' && m.copied === 2 * data.length));
  const dir = await worker.gameDir();
  const storedManifest = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.deepEqual(Array.from(storedManifest.files, f => f.name), ['War3.mpq', 'War3Local.mpq']);
  const stage = await dir.getDirectoryHandle(manifest.generation);
  assert.equal(Buffer.compare(Buffer.from(await (await (await stage.getFileHandle('War3.mpq')).getFile()).arrayBuffer()), data), 0);
  assert.equal(stage.files.has('Setup.mpq'), false);
});

test('standalone MPQ uploads retain their existing manifest format', async () => {
  const worker = makeWorker();
  const output = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(output.at(-1).type, 'done');
  assert.equal(output.at(-1).manifest.bytes, 64);
  assert.deepEqual(Array.from(output.at(-1).manifest.files, f => f.name), ['War3.mpq']);
});

test('rejects expansion-only ISO and does not replace prior installation', async () => {
  const worker = makeWorker();
  const first = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  const existing = first.at(-1).manifest.generation;
  const result = await worker.run([iso([['WAR3X.MPQ', mpq()]])]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /War3.mpq missing/);
  const dir = await worker.gameDir();
  const committed = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(committed.generation, existing);
});

test('rejects ambiguous archive names and invalid MPQ data before staging', async () => {
  const worker = makeWorker();
  const duplicate = await worker.run([iso([['WAR3.MPQ', mpq()]], {duplicate: true})]);
  assert.equal(duplicate.at(-1).type, 'error');
  assert.match(duplicate.at(-1).message, /Ambiguous archive/);
  const invalid = await worker.run([iso([['WAR3.MPQ', Buffer.alloc(64)]])]);
  assert.equal(invalid.at(-1).type, 'error');
  assert.match(invalid.at(-1).message, /Invalid MPQ archive/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('RoC and TFT ISOs produce a combined, engine-compatible manifest', async () => {
  const worker = makeWorker();
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()], ['SETUP.MPQ', setupMpq({count:1})]], {name:'RoC.iso'}),
    iso([['WAR3X.MPQ', mpq()], ['WAR3XLOCAL.MPQ', mpq()]], {name:'TFT.iso'})
  ]);
  assert.equal(result.at(-1).type, 'done');
  const manifest = result.at(-1).manifest;
  assert.deepEqual(Array.from(manifest.files, file => file.name),
    ['War3.mpq', 'War3x.mpq', 'War3xLocal.mpq']);
  assert.equal(manifest.bytes, 3 * mpq().length);
  const stage = await (await worker.gameDir()).getDirectoryHandle(manifest.generation);
  assert.equal((await (await stage.getFileHandle('War3x.mpq')).getFile()).size, 64);
  assert.equal(stage.files.has('Setup.mpq'), false);
});

test('mixed ISO and standalone MPQ inputs work in either order', async () => {
  const worker = makeWorker();
  const result = await worker.run([
    iso([['WAR3X.MPQ', mpq()]], {name:'TFT.iso'}),
    namedBlob(mpq(), 'War3.mpq'), namedBlob(mpq(), 'War3Patch.mpq')
  ]);
  assert.equal(result.at(-1).type, 'done');
  assert.deepEqual(Array.from(result.at(-1).manifest.files, file => file.name),
    ['War3x.mpq', 'War3.mpq', 'War3Patch.mpq']);
  const reverse = await worker.run([
    namedBlob(mpq(), 'War3.mpq'),
    iso([['WAR3X.MPQ', mpq()]], {name:'TFT.iso'})
  ]);
  assert.equal(reverse.at(-1).type, 'done');
});

test('conflicting archives across discs or direct uploads are rejected before OPFS', async () => {
  const worker = makeWorker();
  const a = iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'});
  const b = iso([['WAR3.MPQ', mpq()]], {name:'Other.iso'});
  const duplicates = await worker.run([a, b]);
  assert.equal(duplicates.at(-1).type, 'error');
  assert.match(duplicates.at(-1).message, /Duplicate archive War3.mpq.*Other.iso.*RoC.iso/);
  const direct = await worker.run([a, namedBlob(mpq(), 'WAR3.MPQ')]);
  assert.equal(direct.at(-1).type, 'error');
  assert.match(direct.at(-1).message, /Duplicate archive/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('rejects excessive source count without creating an installation', async () => {
  const worker = makeWorker();
  const inputs = Array.from({length: 9}, (_, index) =>
    iso([['WAR3X.MPQ', mpq()]], {name:`Disc${index}.iso`}));
  const result = await worker.run(inputs);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /Too many ISO/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('estimated storage shortage rejects before staging and preserves prior install', async () => {
  const worker = makeWorker();
  const first = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  const previous = first.at(-1).manifest.generation;
  worker.context.navigator.storage.estimate = async () => ({usage:40, quota:80});
  const failed = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(failed.at(-1).type, 'error');
  assert.match(failed.at(-1).message, /Not enough estimated browser storage/);
  const dir = await worker.gameDir();
  const current = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(current.generation, previous);
  assert.equal(dir.dirs.size, 1);
});

test('unavailable storage estimate does not block import', async () => {
  const worker = makeWorker();
  assert.equal((await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).type, 'done');
  worker.context.navigator.storage.estimate = async () => { throw new Error('denied'); };
  assert.equal((await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).type, 'done');
});

test('OPFS write failure midway through multi-input import rolls back generation', async () => {
  const worker = makeWorker();
  const first = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(first.at(-1).type, 'done');
  const previous = first.at(-1).manifest.generation;
  // Enable failure only after the original generation was committed.
  const dir = await worker.gameDir();
  dir.failWritesFor = 'War3x.mpq';
  const result = await worker.run([
    namedBlob(mpq(), 'War3.mpq'), namedBlob(mpq(), 'War3x.mpq')
  ]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /Simulated OPFS write failure/);
  const manifest = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(manifest.generation, previous);
  assert.equal(dir.dirs.size, 1);
});

test('cancel during copying preserves the previous generation', async () => {
  const worker = makeWorker();
  const first = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  const previous = first.at(-1).manifest.generation;
  // The first progress message is published after the first 1 MiB write.
  const large = mpq(1024 * 1024 + 64);
  worker.context.postMessage = message => {
    worker.messages.push(message);
    if (message.type === 'progress') {
      void worker.context.onmessage({data:{type:'cancel'}});
    }
  };
  const result = await worker.run([namedBlob(large, 'War3.mpq')]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /cancelled/);
  const dir = await worker.gameDir();
  const committed = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(committed.generation, previous);
  assert.equal(dir.dirs.size, 1);
});

test('worker reports scanning, validated discovery, copy progress and commit in order', async () => {
  const worker = makeWorker();
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['WAR3X.MPQ', mpq()]], {name:'TFT.iso'})
  ]);
  const scans = result.filter(message => message.type === 'phase' && message.phase === 'scanning');
  assert.deepEqual(Array.from(scans, message => message.source), ['RoC.iso', 'TFT.iso']);
  const events = result.map(message => message.type === 'phase' ? message.phase : message.type);
  const validating = events.indexOf('validating');
  const discovered = events.indexOf('discovered');
  const storage = events.indexOf('storage');
  const copying = events.indexOf('copying');
  const committing = events.indexOf('committing');
  assert.ok(validating > events.indexOf('scanning'));
  assert.ok(discovered > validating);
  assert.ok(storage > discovered);
  assert.ok(copying > storage);
  assert.ok(committing > events.indexOf('progress'));
  assert.ok(events.indexOf('done') > committing);
  const entry = result[discovered];
  assert.equal(entry.bytes, 128);
  assert.deepEqual(Array.from(entry.files, f => [f.name, f.size, f.source]),
    [['War3.mpq', 64, 'RoC.iso'], ['War3x.mpq', 64, 'TFT.iso']]);
  const copied = result.filter(message => message.type === 'progress');
  assert.deepEqual(Array.from(copied, m => [m.index, m.count, m.fileCopied, m.fileSize]),
    [[1, 2, 64, 64], [2, 2, 64, 64]]);
});

test('invalid archive does not announce a valid discovered file set', async () => {
  const worker = makeWorker();
  const result = await worker.run([iso([['WAR3.MPQ', Buffer.alloc(64)]])]);
  assert.equal(result.at(-1).type, 'error');
  assert.equal(result.some(message => message.type === 'discovered'), false);
  assert.equal(result.some(message => message.type === 'committing'), false);
});


test('recognized retail Setup.mpq layout imports maps into nested paths', async () => {
  const worker = makeWorker();
  const output = await worker.run([iso([
    ['WAR3.MPQ', mpq()], ['SETUP.MPQ', setupMpq()]
  ])]);
  assert.equal(output.at(-1).type, 'done');
  const manifest = output.at(-1).manifest;
  assert.equal(manifest.version, 3);
  assert.equal(manifest.files.length, 45);
  assert.ok(manifest.files.some(entry => entry.name === 'Maps/(4)LostTemple.w3m'));
  assert.ok(manifest.files.some(entry => entry.name === 'Maps/Scenario/(4)WarChasers.w3m'));
  assert.equal(manifest.files.some(entry => entry.name.toLowerCase() === 'setup.mpq'), false);
  const dir = await worker.gameDir();
  const stage = await dir.getDirectoryHandle(manifest.generation);
  const maps = await stage.getDirectoryHandle('Maps');
  const map = await (await maps.getFileHandle('(4)LostTemple.w3m')).getFile();
  assert.equal(map.size, 256);
  assert.equal(Buffer.from(await map.arrayBuffer()).subarray(0,4).toString('latin1'), 'MPQ\x1a');
});

test('zlib sector-compressed Setup.mpq entry is restored to a valid map', async () => {
  const worker = makeWorker();
  const output = await worker.run([namedBlob(mpq(), 'War3.mpq'),
    namedBlob(setupMpq({compressed:true}), 'Setup.mpq')]);
  assert.equal(output.at(-1).type, 'done');
  const dir = await worker.gameDir();
  const stage = await dir.getDirectoryHandle(output.at(-1).manifest.generation);
  const maps = await stage.getDirectoryHandle('Maps');
  assert.equal((await (await maps.getFileHandle('(10)DustwallowKeys.w3m')).getFile()).size, 256);
});

test('unknown Setup.mpq layout reports skipped maps without installing container', async () => {
  const worker = makeWorker();
  const output = await worker.run([namedBlob(mpq(), 'War3.mpq'),
    namedBlob(setupMpq({count:1}), 'Setup.mpq')]);
  assert.equal(output.at(-1).type, 'done');
  assert.equal(output.at(-1).manifest.files.length, 1);
  assert.ok(output.some(message => message.type === 'notice' && /Unknown Setup/.test(message.message)));
});

test('unsupported map codec fails before replacing previous manifest', async () => {
  const worker = makeWorker();
  const existing = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const output = await worker.run([namedBlob(mpq(), 'War3.mpq'),
    namedBlob(setupMpq({compressed:true, unsupported:true}), 'Setup.mpq')]);
  assert.equal(output.at(-1).type, 'error');
  assert.match(output.at(-1).message, /Unsupported Setup.mpq map compression/);
  const dir = await worker.gameDir();
  const manifest = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(manifest.generation, existing);
});

test('TFT installer with explicitly named expansion MPQs is imported with RoC', async () => {
  const worker = makeWorker();
  const original = mpq(1664, 0x5a);
  const local = mpq(324, 0x38);
  const tft = setupMpq({entries:[
    {name:'War3x.mpq', data:original, compressed:true},
    {name:'War3xLocal.mpq', data:local, compressed:false}
  ]});
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'Retail-RoC.iso'}),
    iso([['SETUP.MPQ', tft]], {name:'Retail-TFT.iso'})
  ]);
  assert.equal(result.at(-1).type, 'done');
  assert.deepEqual(Array.from(result.at(-1).manifest.files, entry => entry.name),
    ['War3.mpq', 'War3x.mpq', 'War3xLocal.mpq']);
  const stage = await (await worker.gameDir()).getDirectoryHandle(result.at(-1).manifest.generation);
  for (const [name, expected] of [['War3x.mpq', original], ['War3xLocal.mpq', local]]) {
    const stored = Buffer.from(await (await (await stage.getFileHandle(name)).getFile()).arrayBuffer());
    assert.equal(Buffer.compare(stored, expected), 0);
  }
  assert.equal(stage.files.has('Setup.mpq'), false);
});

test('unknown TFT installer mapping fails instead of committing incomplete expansion', async () => {
  const worker = makeWorker();
  const existing = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({count:1})]], {name:'Frozen-Throne.iso'})
  ]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /Could not recover War3x.mpq/);
  const dir = await worker.gameDir();
  const manifest = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(manifest.generation, existing);
  assert.equal(dir.dirs.size, 1);
  assert.ok(result.some(message => message.type === 'notice' && /No named expansion/.test(message.message)));
});

test('a TFT installer holding only localized data is not considered a complete expansion', async () => {
  const worker = makeWorker();
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({entries:[
      {name:'War3xLocal.mpq', data:mpq()}
    ]})]], {name:'TFT.iso'})
  ]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /War3xLocal.mpq requires War3x.mpq in the same installation/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('a malformed explicitly named expansion archive is rejected safely', async () => {
  const worker = makeWorker();
  const broken = Buffer.alloc(64, 0);
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({entries:[
      {name:'War3x.mpq', data:broken, compressed:true}
    ]})]], {name:'TFT.iso'})
  ]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /not an MPQ/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('2002 demo ISO imports base data and RetailMaps with a persistent demo label', async () => {
  const worker = makeWorker();
  const demoMap = mpq(256);
  const result = await worker.run([iso([
    ['WAR3.MPQ', mpq()], ['WAR3DEMO.ICO', Buffer.alloc(64)],
    ['SCDEMO.EXE', Buffer.alloc(64)],
    ['SETUP.MPQ', Buffer.alloc(64)] // Never apply retail installer-map mappings here.
  ], {name:'Retail-Warcraft-III.iso', retailMaps:[
    ['(4)Roundabout.w3m', demoMap], ['(6)EnakrosWay.w3m', mpq()]
  ]})]);
  assert.equal(result.at(-1).type, 'done');
  assert.equal(result.at(-1).manifest.edition, 'demo');
  assert.equal(result.at(-1).manifest.completeness, 'demo');
  assert.deepEqual(Array.from(result.at(-1).manifest.files, file => file.name),
    ['War3.mpq', 'Maps/(4)Roundabout.w3m', 'Maps/(6)EnakrosWay.w3m']);
  assert.ok(result.some(message => message.type === 'notice' &&
    /Warcraft III Demo disc detected/.test(message.message)));
  const dir = await worker.gameDir();
  const stored = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(stored.edition, 'demo');
  const stage = await dir.getDirectoryHandle(stored.generation);
  const maps = await stage.getDirectoryHandle('Maps');
  assert.equal(Buffer.compare(Buffer.from(await (await (await maps.getFileHandle(
    '(4)Roundabout.w3m')).getFile()).arrayBuffer()), demoMap), 0);
});

test('mixing demo and retail sources is rejected without replacing an installation', async () => {
  const worker = makeWorker();
  const existing = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest;
  const demo = iso([['WAR3.MPQ', mpq()], ['WAR3DEMO.ICO', Buffer.alloc(64)],
    ['SCDEMO.EXE', Buffer.alloc(64)]], {name:'Demo.iso'});
  const result = await worker.run([demo, iso([['WAR3X.MPQ', mpq()]], {name:'TFT.iso'})]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /Demo discs must be imported separately/);
  const dir = await worker.gameDir();
  const stored = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(stored.generation, existing.generation);
  const mixed = await worker.run([demo, namedBlob(mpq(), 'War3x.mpq')]);
  assert.equal(mixed.at(-1).type, 'error');
  assert.match(mixed.at(-1).message, /Demo discs must be imported separately/);
});

// Anonymous game MPQs are identified from their own hash tables, not by a
// guessed installer FileNNN name, byte offset or MPQ size. The synthetic
// archives below use harmless dummy content with documented WC3 path names.
function nestedGame(paths) {
  return setupMpq({entries:paths.map((name, i) => ({
    name, data:Buffer.concat([mpq(), Buffer.alloc(i + 1)])
  }))});
}
const EXPANSION_PATHS = [
  'Melee_V0\\Units\\UnitBalance.slk',
  'Custom_V0\\Units\\UnitBalance.slk',
  'Custom_V1\\Units\\UnitBalance.slk'
];
const EXPANSION_LOCAL_PATHS = [
  'Maps\\FrozenThrone\\Campaign\\HumanX01.w3x',
  'Maps\\FrozenThrone\\Campaign\\UndeadX01.w3x',
  'Maps\\FrozenThrone\\Campaign\\NightElfX01.w3x'
];

test('anonymous embedded TFT archives are classified by internal file hashes', async () => {
  const worker = makeWorker();
  const main = nestedGame(EXPANSION_PATHS);
  const local = nestedGame(EXPANSION_LOCAL_PATHS);
  // Match the reported TFT installer layout: anonymous MPQ blocks 155/156,
  // surrounded by unrelated installer members. No hard-coded offsets needed.
  const members = Array.from({length:245}, (_, i) => ({
    name:'File' + String(i).padStart(8, '0') + '.w3x', data:mpq()
  }));
  members[155].data = main;
  members[156].data = local;
  const output = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({entries:members})]], {name:'TFT.iso'})
  ]);
  assert.equal(output.at(-1).type, 'done');
  assert.deepEqual(Array.from(output.at(-1).manifest.files, entry => entry.name),
    ['War3.mpq', 'War3x.mpq', 'War3xLocal.mpq']);
  const stage = await (await worker.gameDir()).getDirectoryHandle(output.at(-1).manifest.generation);
  for (const [name, expected] of [['War3x.mpq', main], ['War3xLocal.mpq', local]]) {
    const actual = Buffer.from(await (await (await stage.getFileHandle(name)).getFile()).arrayBuffer());
    assert.equal(Buffer.compare(actual, expected), 0);
  }
  assert.equal(stage.files.has('Setup.mpq'), false);
});

test('named non-expansion MPQs in an installer are imported without guessing roles', async () => {
  const worker = makeWorker();
  const baseLocal = nestedGame(['Maps\\Campaign\\Human01.w3m']);
  const patch = nestedGame(['Units\\UnitBalance.slk']);
  const output = await worker.run([
    namedBlob(mpq(), 'War3.mpq'),
    namedBlob(setupMpq({entries:[
      {name:'War3Local.mpq', data:baseLocal},
      {name:'War3Patch.mpq', data:patch}
    ]}), 'Setup.mpq')
  ]);
  assert.equal(output.at(-1).type, 'done');
  assert.deepEqual(Array.from(output.at(-1).manifest.files, entry => entry.name),
    ['War3.mpq', 'War3Local.mpq', 'War3Patch.mpq']);
});

test('anonymous base and localized archives require their own fingerprints', async () => {
  const worker = makeWorker();
  const base = nestedGame([
    'Scripts\\common.j', 'Scripts\\human.ai',
    'UI\\FrameDef\\Glue\\StandardTemplates.fdf'
  ]);
  const local = nestedGame([
    'Maps\\Campaign\\Human01.w3m', 'Maps\\Campaign\\Undead01.w3m'
  ]);
  const output = await worker.run([namedBlob(setupMpq({entries:[
    {name:'X001.dat',data:base},{name:'X002.dat',data:local}
  ]}), 'Setup.mpq')]);
  assert.equal(output.at(-1).type, 'done');
  assert.deepEqual(Array.from(output.at(-1).manifest.files, e => e.name),
    ['War3.mpq','War3Local.mpq']);
});

test('anonymous nested MPQ with ambiguous evidence is never assigned an archive name', async () => {
  const worker = makeWorker();
  const ambiguous = nestedGame([...EXPANSION_PATHS, ...EXPANSION_LOCAL_PATHS]);
  const output = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({entries:[
      {name:'unknown.blob', data:ambiguous}
    ]})]], {name:'TFT.iso'})
  ]);
  assert.equal(output.at(-1).type, 'error');
  assert.match(output.at(-1).message, /Could not recover War3x.mpq/);
  assert.ok(output.some(m => m.type === 'notice' && /could not be identified/.test(m.message)));
  await assert.rejects(worker.gameDir(), e => e.name === 'NotFoundError');
});

test('anonymous archives with too little evidence are skipped, but named expansion still works', async () => {
  const worker = makeWorker();
  const unknown = nestedGame([EXPANSION_PATHS[0]]);
  const named = nestedGame(EXPANSION_PATHS);
  const output = await worker.run([
    namedBlob(mpq(), 'War3.mpq'),
    namedBlob(setupMpq({entries:[
      {name:'War3x.mpq', data:named},
      {name:'unknown.dat',data:unknown}
    ]}), 'Setup.mpq')
  ]);
  assert.equal(output.at(-1).type, 'done');
  assert.deepEqual(Array.from(output.at(-1).manifest.files, e => e.name),
    ['War3.mpq', 'War3x.mpq']);
  assert.ok(output.some(m => m.type === 'notice' && /1 embedded MPQ/.test(m.message)));
});

test('duplicate archive names across an ISO and a Setup.mpq are rejected before committing', async () => {
  const worker = makeWorker();
  const existing = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const output = await worker.run([
    iso([['WAR3.MPQ', mpq()]], {name:'RoC.iso'}),
    iso([['SETUP.MPQ', setupMpq({entries:[
      {name:'War3.mpq', data:nestedGame(EXPANSION_PATHS)},
      {name:'War3x.mpq', data:nestedGame(EXPANSION_PATHS)}
    ]})]], {name:'TFT.iso'})
  ]);
  assert.equal(output.at(-1).type, 'error');
  assert.match(output.at(-1).message, /Duplicate archive War3.mpq/);
  const dir = await worker.gameDir();
  const manifest = JSON.parse(await (await (await dir.getFileHandle('installation.json')).getFile()).text());
  assert.equal(manifest.generation, existing);
});

// Lock/manifest/cleanup regressions operate on the actual worker handler.
test('rejects forged headers with invalid length, counts, or out-of-file tables', async () => {
  const worker = makeWorker();
  for (const corrupt of [
    data => data.writeUInt32LE(100000, 8),
    data => data.writeUInt32LE(3, 24),
    data => data.writeUInt32LE(52, 20),
    data => data.writeUInt16LE(20, 14),
    data => data.writeUInt32LE(63, 8)
  ]) {
    const data = mpq();
    corrupt(data);
    const result = await worker.run([namedBlob(data, 'War3.mpq')]);
    assert.equal(result.at(-1).type, 'error');
    assert.match(result.at(-1).message, /Invalid MPQ archive/);
  }
  await assert.rejects(worker.gameDir(), err => err.name === 'NotFoundError');
});

test('an MPQ with excessive trailing bytes is refused', async () => {
  const worker = makeWorker();
  const data = mpq(8192);
  data.writeUInt32LE(64, 8);
  const result = await worker.run([namedBlob(data, 'War3.mpq')]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /excessive trailing data/);
});

test('stale generations are pruned before the next quota preflight', async () => {
  const worker = makeWorker();
  const first = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const second = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const dir = await worker.gameDir();
  worker.context.navigator.storage.estimate = async () => {
    // The earlier unused generation must have been removed before estimating.
    assert.equal(dir.dirs.has(first), false);
    assert.equal(dir.dirs.has(second), true);
    return {usage:0, quota:4096};
  };
  const result = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(result.at(-1).type, 'done');
});

test('older unleased OPFS generations are reclaimed, retaining current and prior', async () => {
  const worker = makeWorker();
  const a = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const b = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const c = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const dir = await worker.gameDir();
  assert.equal(dir.dirs.has(a), false);
  assert.equal(dir.dirs.has(b), true);
  assert.equal(dir.dirs.has(c), true);
});

test('leased older generation is not pruned until a later import', async () => {
  const worker = makeWorker();
  const a = (await worker.run([namedBlob(mpq(), 'War3.mpq')])).at(-1).manifest.generation;
  const release = worker.locks.hold('openrealm-wc3-generation:' + a);
  await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal((await worker.gameDir()).dirs.has(a), true);
  release();
  await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal((await worker.gameDir()).dirs.has(a), false);
});

test('cross-tab writer lock prevents import, and leased generation blocks removal', async () => {
  const worker = makeWorker();
  const locked = worker.locks.hold('openrealm-wc3-installation', 'exclusive');
  const denied = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(denied.at(-1).type, 'error');
  assert.match(denied.at(-1).message, /Another browser tab/);
  locked();
  const installed = await worker.run([namedBlob(mpq(), 'War3.mpq')]);
  assert.equal(installed.at(-1).type, 'done');
  const gen = installed.at(-1).manifest.generation;
  const release = worker.locks.hold('openrealm-wc3-generation:' + gen);
  await worker.context.onmessage({data:{type:'remove'}});
  assert.match(worker.messages.at(-1).message, /Close other Warcraft III game tabs/);
  assert.equal((await worker.gameDir()).dirs.has(gen), true);
  release();
  await worker.context.onmessage({data:{type:'remove'}});
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('missing War3xLocal marks partial expansion instead of claiming full content', async () => {
  const worker = makeWorker();
  const result = await worker.run([namedBlob(mpq(), 'War3.mpq'), namedBlob(mpq(), 'War3x.mpq')]);
  assert.equal(result.at(-1).type, 'done');
  assert.equal(result.at(-1).manifest.completeness, 'partial-expansion');
  assert.deepEqual(Array.from(result.at(-1).manifest.missing), ['War3xLocal.mpq']);
  assert.ok(result.some(message => message.type === 'notice' && /Partial expansion/.test(message.message)));
});

test('realistic multi-sector fixture preserves nonoverlapping source extents', async () => {
  const worker = makeWorker();
  const large = mpq(2048 * 3 + 100, 0x49);
  const base = mpq(2048 * 2 + 51, 0x53);
  const result = await worker.run([iso([['WAR3.MPQ', large], ['WAR3X.MPQ', base]])]);
  assert.equal(result.at(-1).type, 'done');
  const stage = await (await worker.gameDir()).getDirectoryHandle(result.at(-1).manifest.generation);
  const a = Buffer.from(await (await (await stage.getFileHandle('War3.mpq')).getFile()).arrayBuffer());
  const b = Buffer.from(await (await (await stage.getFileHandle('War3x.mpq')).getFile()).arrayBuffer());
  assert.equal(Buffer.compare(a, large), 0);
  assert.equal(Buffer.compare(b, base), 0);
});
