/* Exercise the real browser installer worker with synthetic ISO/MPQ bytes only. */
'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const SITE = path.resolve(__dirname, '../site');
const BLOCK = 2048;

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
function mpq() {
  const bytes = Buffer.alloc(64);
  bytes.write('MPQ', 0, 'ascii');
  bytes[3] = 0x1a;
  return bytes;
}
function iso(entries, {duplicate = false, name = 'Disc.iso'} = {}) {
  const sectors = 48;
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
    sector++;
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
}
function makeWorker({estimate, failWritesFor = null} = {}) {
  const root = new MemoryDir(failWritesFor);
  const messages = [];
  const storage = {getDirectory: async () => root};
  if (estimate !== undefined) storage.estimate = async () => estimate;
  const context = vm.createContext({
    Blob, AbortController, Uint8Array, DataView, Number, Date, Math,
    navigator: {storage},
    postMessage: message => messages.push(message)
  });
  context.importScripts = (...scripts) => {
    for (const script of scripts) {
      assert.equal(script, 'iso9660.js');
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
  return {run, messages, context, gameDir};
}

test('ISO import copies only recognized archive extents and writes v2 manifest', async () => {
  const worker = makeWorker();
  const data = mpq();
  const output = await worker.run([iso([
    ['WAR3.MPQ', data], ['SETUP.MPQ', Buffer.alloc(64)],
    ['WAR3LOCAL.MPQ', data], ['README.TXT', Buffer.from('ignored')]
  ])]);
  assert.equal(output.at(-1).type, 'done');
  const manifest = output.at(-1).manifest;
  assert.equal(manifest.version, 2);
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
  assert.match(invalid.at(-1).message, /No supported MPQ header/);
  await assert.rejects(worker.gameDir(), error => error.name === 'NotFoundError');
});

test('RoC and TFT ISOs produce a combined, engine-compatible manifest', async () => {
  const worker = makeWorker();
  const result = await worker.run([
    iso([['WAR3.MPQ', mpq()], ['SETUP.MPQ', Buffer.alloc(64)]], {name:'RoC.iso'}),
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
  const large = Buffer.alloc(1024 * 1024 + 64);
  mpq().copy(large, 0);
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
