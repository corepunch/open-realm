/* Synthetic ISO fixtures only: no retail Warcraft III files are required. */
'use strict';
const assert = require('node:assert/strict');
const {test} = require('node:test');
const {openISO, ISO9660Error} = require('../site/iso9660.js');

const BLOCK = 2048;
const SECTORS = 40;

function put16Both(data, index, number) {
  data.writeUInt16LE(number, index);
  data.writeUInt16BE(number, index + 2);
}

function put32Both(data, index, number) {
  data.writeUInt32LE(number, index);
  data.writeUInt32BE(number, index + 4);
}

// ISO identifiers use either one-byte ISO 9660 or two-byte big-endian Joliet.
function identifier(name, joliet) {
  if (typeof name === 'number') return Buffer.from([name]);
  if (!joliet) return Buffer.from(name, 'latin1');
  const data = Buffer.alloc(name.length * 2);
  for (let i = 0; i < name.length; i++) data.writeUInt16BE(name.charCodeAt(i), i * 2);
  return data;
}

function record(name, extent, size, {directory = false, joliet = false, flags = 0, xattr = 0} = {}) {
  const id = identifier(name, joliet);
  const length = 33 + id.length + (id.length % 2 === 0 ? 1 : 0);
  const bytes = Buffer.alloc(length);
  bytes[0] = length;
  bytes[1] = xattr;
  put32Both(bytes, 2, extent);
  put32Both(bytes, 10, size);
  bytes[25] = (directory ? 2 : 0) | flags;
  put16Both(bytes, 28, 1);
  bytes[32] = id.length;
  id.copy(bytes, 33);
  return bytes;
}

function writeDirectory(image, extent, entries) {
  let pos = extent * BLOCK;
  for (const entry of entries) {
    const sectorLeft = BLOCK - (pos % BLOCK);
    if (entry.length > sectorLeft) pos += sectorLeft;
    entry.copy(image, pos);
    pos += entry.length;
  }
}

function descriptor(image, sector, type, rootExtent, rootBytes, joliet) {
  const data = image.subarray(sector * BLOCK, (sector + 1) * BLOCK);
  data[0] = type;
  data.write('CD001', 1, 'ascii');
  data[6] = 1;
  if (type === 255) return;
  put32Both(data, 80, SECTORS);
  put16Both(data, 128, BLOCK);
  if (joliet) data.write('%/E', 88, 'ascii');
  record(0, rootExtent, rootBytes, {directory: true}).copy(data, 156);
}

// Root spans two blocks to exercise zero-padding jumps and nested traversal.
function fixture({joliet = false} = {}) {
  const image = Buffer.alloc(SECTORS * BLOCK);
  descriptor(image, 16, 1, 20, 2 * BLOCK, false);
  if (joliet) descriptor(image, 17, 2, 22, BLOCK, true);
  descriptor(image, joliet ? 18 : 17, 255);
  const root = [
    record(0, 20, 2 * BLOCK, {directory: true}),
    record(1, 20, 2 * BLOCK, {directory: true}),
    record('WAR3.MPQ;1', 24, 4),
    record('DATA', 23, BLOCK, {directory: true})
  ];
  writeDirectory(image, 20, root);
  writeDirectory(image, 21, [record('SECOND.W3M;1', 26, 2)]);
  writeDirectory(image, 23, [
    record(0, 23, BLOCK, {directory: true}),
    record(1, 20, 2 * BLOCK, {directory: true}),
    record('EXTRA.MPQ;1', 25, 3)
  ]);
  image.write('MPQ!', 24 * BLOCK);
  image.write('XYZ', 25 * BLOCK);
  image.write('OK', 26 * BLOCK);

  if (joliet) {
    writeDirectory(image, 22, [
      record(0, 22, BLOCK, {directory: true, joliet: true}),
      record(1, 22, BLOCK, {directory: true, joliet: true}),
      record('War3.mpq;1', 24, 4, {joliet: true}),
      record('Café.w3x;1', 26, 2, {joliet: true})
    ]);
  }
  return image;
}

async function inspect(data) {
  return openISO(new Blob([data]));
}

test('reads primary ISO directory, nested files, sector padding, and offsets', async () => {
  const data = fixture();
  const image = await inspect(data);
  const entries = await image.listFiles();
  assert.equal(image.joliet, false);
  assert.equal(image.sectorSize, BLOCK);
  assert.deepEqual(entries.map(x => x.path).sort(),
    ['DATA/EXTRA.MPQ', 'SECOND.W3M', 'WAR3.MPQ']);
  assert.deepEqual(await image.findFiles('war3.mpq'), [
    {path: 'WAR3.MPQ', name: 'WAR3.MPQ', offset: 24 * BLOCK, size: 4}
  ]);
  assert.equal((await image.findFiles('data/extra.mpq'))[0].offset, 25 * BLOCK);
  const gameFile = (await image.findFiles('WAR3.MPQ'))[0];
  assert.equal(Buffer.from(await new Blob([data]).slice(
    gameFile.offset, gameFile.offset + gameFile.size).arrayBuffer()).toString(), 'MPQ!');
  assert.equal(await image.listFiles(), entries);
  assert.ok(Object.isFrozen(entries));
  assert.ok(Object.isFrozen(entries[0]));
});

test('prefers Joliet names and strips numeric ISO file versions', async () => {
  const image = await inspect(fixture({joliet: true}));
  assert.equal(image.joliet, true);
  const names = (await image.listFiles()).map(x => x.name);
  assert.deepEqual(names, ['War3.mpq', 'Café.w3x']);
  assert.equal((await image.findFiles('CAFÉ.W3X'))[0].size, 2);
});

test('rejects non-ISO bytes and images missing descriptor termination', async () => {
  await assert.rejects(inspect(Buffer.alloc(SECTORS * BLOCK)), ISO9660Error);
  const data = fixture();
  data.fill(0, 17 * BLOCK, 18 * BLOCK);
  await assert.rejects(inspect(data), /supported ISO 9660 volume/);
});

test('requires a primary descriptor even if a Joliet descriptor is present', async () => {
  const data = fixture({joliet: true});
  data[16 * BLOCK] = 0;
  await assert.rejects(inspect(data), /primary volume descriptor missing/);
});

test('checks declared volume size and directory extents', async () => {
  const truncated = fixture();
  put32Both(truncated, 16 * BLOCK + 80, SECTORS + 1);
  await assert.rejects(inspect(truncated), /volume is truncated/);

  const outOfRange = fixture();
  put32Both(outOfRange, 20 * BLOCK + 2 * 34 + 2, SECTORS);
  const iso = await inspect(outOfRange);
  await assert.rejects(iso.listFiles(), /extent extends outside image/);
});

test('rejects directory records crossing a logical sector boundary', async () => {
  const data = fixture();
  // 60 legal 34-byte special records occupy 2040 bytes of the first block.
  // The next record claims to extend beyond the 2048-byte sector boundary.
  data.fill(0, 20 * BLOCK, 21 * BLOCK);
  const special = record(0, 20, 2 * BLOCK, {directory: true});
  for (let i = 0; i < 60; i++) special.copy(data, 20 * BLOCK + i * 34);
  data[21 * BLOCK - 8] = 40;
  const iso = await inspect(data);
  await assert.rejects(iso.listFiles(), /crosses sector boundary/);
});

test('detects reused directory extents instead of entering cycles', async () => {
  const data = fixture();
  // DATA points back at the root rather than at its own directory.
  const location = 20 * BLOCK + 34 + 34 + record('WAR3.MPQ;1', 24, 4).length;
  put32Both(data, location + 2, 20);
  put32Both(data, location + 10, 2 * BLOCK);
  const iso = await inspect(data);
  await assert.rejects(iso.listFiles(), /Repeated or cyclic/);
});

test('rejects multi-extent and interleaved records rather than copying partial data', async () => {
  const multi = fixture();
  const position = 20 * BLOCK + 2 * 34;
  multi[position + 25] |= 0x80;
  await assert.rejects((await inspect(multi)).listFiles(), /Multi-extent/);

  const interleaved = fixture();
  interleaved[position + 26] = 1;
  await assert.rejects((await inspect(interleaved)).listFiles(), /Interleaved/);
});

test('normalizes directory separators and rejects duplicate output paths', async () => {
  const unsafe = fixture({joliet: true});
  unsafe.fill(0, 22 * BLOCK, 23 * BLOCK);
  writeDirectory(unsafe, 22, [
    record(0, 22, BLOCK, {directory: true, joliet: true}),
    record(1, 22, BLOCK, {directory: true, joliet: true}),
    record('../escape;1', 24, 4, {joliet: true})
  ]);
  // The embedded slash never escapes the root path (it becomes an underscore).
  const paths = (await (await inspect(unsafe)).listFiles()).map(x => x.path);
  assert.deepEqual(paths, ['.._escape']);

  const duplicate = fixture();
  writeDirectory(duplicate, 21, [record('WAR3.MPQ;2', 26, 2)]);
  await assert.rejects((await inspect(duplicate)).listFiles(), /Duplicate ISO path/);
});

test('rejects an unsupported Joliet surrogate identifier', async () => {
  const data = fixture({joliet: true});
  const pos = 22 * BLOCK + 2 * 34 + 33;
  data.writeUInt16BE(0xd800, pos);
  await assert.rejects((await inspect(data)).listFiles(), /surrogate/);
});

test('rejects inconsistent byte-order copies and unsupported sector sizes', async () => {
  const badExtent = fixture();
  badExtent.writeUInt32BE(31, 20 * BLOCK + 2 * 34 + 6);
  await assert.rejects((await inspect(badExtent)).listFiles(), /Mismatched ISO 9660/);

  const badSector = fixture();
  put16Both(badSector, 16 * BLOCK + 128, 1024);
  await assert.rejects(inspect(badSector), /logical sector size/);
});

test('rejects incomplete Joliet identifiers', async () => {
  const data = fixture({joliet: true});
  const position = 22 * BLOCK + 2 * 34;
  data[position + 32] = 3;
  await assert.rejects((await inspect(data)).listFiles(), /Odd-length Joliet/);
});

test('honors cancellation before and during inspection', async () => {
  const controller = new AbortController();
  controller.abort();
  await assert.rejects(openISO(new Blob([fixture()]), {signal: controller.signal}),
    error => error.name === 'AbortError');
  const next = new AbortController();
  const iso = await openISO(new Blob([fixture()]), {signal: next.signal});
  next.abort();
  await assert.rejects(iso.listFiles(), error => error.name === 'AbortError');
});

test('rejects invalid query types', async () => {
  const iso = await inspect(fixture());
  await assert.rejects(iso.findFiles(''), TypeError);
  await assert.rejects(iso.findFiles(null), TypeError);
});
