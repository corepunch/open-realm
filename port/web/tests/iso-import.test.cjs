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
function iso(entries, {duplicate = false} = {}) {
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
  return namedBlob(bytes, 'Disc.iso');
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
  constructor() { this.value = new Blob([]); }
  async getFile() { return this.value; }
  async createWritable() {
    const buffers = [];
    const handle = this;
    return {
      async write(data) { buffers.push(data); },
      async close() { handle.value = new Blob(buffers); },
      async abort() { buffers.length = 0; }
    };
  }
}
class MemoryDir {
  constructor() { this.dirs = new Map(); this.files = new Map(); }
  async getDirectoryHandle(name, {create = false} = {}) {
    if (!this.dirs.has(name)) {
      if (!create) missing(name);
      this.dirs.set(name, new MemoryDir());
    }
    return this.dirs.get(name);
  }
  async getFileHandle(name, {create = false} = {}) {
    if (!this.files.has(name)) {
      if (!create) missing(name);
      this.files.set(name, new MemoryFile());
    }
    return this.files.get(name);
  }
  async removeEntry(name) {
    if (!this.dirs.delete(name) && !this.files.delete(name)) missing(name);
  }
}
function makeWorker() {
  const root = new MemoryDir();
  const messages = [];
  const context = vm.createContext({
    Blob, AbortController, Uint8Array, DataView, Number, Date, Math,
    navigator: {storage: {getDirectory: async () => root}},
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
  assert.match(result.at(-1).message, /Reign of Chaos base disc/);
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

test('rejects mixed and multiple ISO sources explicitly', async () => {
  const worker = makeWorker();
  const disc = iso([['WAR3.MPQ', mpq()]]);
  const result = await worker.run([disc, namedBlob(mpq(), 'War3Local.mpq')]);
  assert.equal(result.at(-1).type, 'error');
  assert.match(result.at(-1).message, /multi-disc imports/);
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
