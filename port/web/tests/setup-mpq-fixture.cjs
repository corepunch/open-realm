/* Synthetic, redistributable Setup.mpq fixtures. No Blizzard assets. */
'use strict';
const zlib = require('node:zlib');
const MAP_COUNT = 44;
const storm = new Uint32Array(0x500);
let seed = 0x100001;
for (let i = 0; i < 256; i++) for (let j = i; j < 1280; j += 256) {
  seed = (seed * 125 + 3) % 0x2aaaab;
  const high = seed & 0xffff;
  seed = (seed * 125 + 3) % 0x2aaaab;
  storm[j] = ((high << 16) | (seed & 0xffff)) >>> 0;
}
function hash(name, type) {
  let x = 0x7fed7fed, y = 0xeeeeeeee;
  for (const c of name.toUpperCase()) {
    const code = c.charCodeAt(0);
    x = (storm[256 * type + code] ^ ((x + y) >>> 0)) >>> 0;
    y = (code + x + y + (y << 5) + 3) >>> 0;
  }
  return x;
}
function encrypt(bytes, key) {
  let y = 0xeeeeeeee;
  for (let i = 0; i < bytes.length; i += 4) {
    const mixed = (y + storm[1024 + (key & 255)]) >>> 0;
    const plain = bytes.readUInt32LE(i);
    bytes.writeUInt32LE((plain ^ ((key + mixed) >>> 0)) >>> 0, i);
    key = (((~key << 21) + 0x11111111) | (key >>> 11)) >>> 0;
    y = (plain + mixed + (mixed << 5) + 3) >>> 0;
  }
}
function mapBytes(length = 256) {
  const bytes = Buffer.alloc(length, 0x41);
  bytes.write('MPQ', 0, 'ascii'); bytes[3] = 0x1a;
  bytes.writeUInt32LE(32, 4);
  bytes.writeUInt32LE(length, 8);
  bytes.writeUInt32LE(32, 16);
  bytes.writeUInt32LE(48, 20);
  bytes.writeUInt32LE(1, 24);
  bytes.writeUInt32LE(1, 28);
  return bytes;
}
function setupMpq({count = MAP_COUNT, compressed = false, unsupported = false,
  entries = null} = {}) {
  const members = entries || Array.from({length:count}, (_, i) => ({
    name:'File' + String(i + 69).padStart(8, '0') + '.w3x', data:mapBytes(),
    compressed:compressed && i === 0, unsupported:unsupported && i === 0
  }));
  count = members.length;
  // Support synthetic installer block tables as large as the observed 245
  // entries; reserve spare hash slots for linear probing.
  let hashSize = 128;
  while (hashSize < count * 2) hashSize *= 2;
  const hashTable = Buffer.alloc(hashSize * 16, 0xff);
  const blocks = Buffer.alloc(count * 16);
  const headerSize = 32;
  const firstOffset = headerSize + hashTable.length + blocks.length;
  const payloads = [];
  let offset = firstOffset;
  for (let i = 0; i < count; i++) {
    const {name, data:map, compressed:useCompression, unsupported:badCodec} = members[i];
    let stored = map, flags = 0x80000000;
    if (useCompression) {
      const sectors = Math.ceil(map.length / 512);
      const offsets = Buffer.alloc((sectors + 1) * 4);
      const parts = [];
      let sectorOffset = offsets.length;
      for (let sector = 0; sector < sectors; ++sector) {
        const raw = map.subarray(sector * 512, Math.min(map.length, (sector + 1) * 512));
        const method = Buffer.concat([Buffer.from([badCodec ? 0x08 : 0x02]), zlib.deflateSync(raw)]);
        offsets.writeUInt32LE(sectorOffset, sector * 4);
        sectorOffset += method.length;
        parts.push(method);
      }
      offsets.writeUInt32LE(sectorOffset, sectors * 4);
      stored = Buffer.concat([offsets, ...parts]);
      flags |= 0x200;
    }
    blocks.writeUInt32LE(offset, i * 16);
    blocks.writeUInt32LE(stored.length, i * 16 + 4);
    blocks.writeUInt32LE(map.length, i * 16 + 8);
    blocks.writeUInt32LE(flags, i * 16 + 12);
    payloads.push(stored);
    offset += stored.length;
    let slot = hash(name, 0) & (hashSize - 1);
    while (hashTable.readUInt32LE(slot * 16 + 12) !== 0xffffffff) slot = (slot + 1) & (hashSize - 1);
    hashTable.writeUInt32LE(hash(name, 1), slot * 16);
    hashTable.writeUInt32LE(hash(name, 2), slot * 16 + 4);
    hashTable.writeUInt32LE(0, slot * 16 + 8);
    hashTable.writeUInt32LE(i, slot * 16 + 12);
  }
  encrypt(hashTable, 0xc3af3770);
  encrypt(blocks, 0xec83b3a3);
  const header = Buffer.alloc(headerSize);
  header.write('MPQ', 0, 'ascii'); header[3] = 0x1a;
  header.writeUInt32LE(headerSize, 4);
  header.writeUInt32LE(offset, 8);
  header.writeUInt32LE(headerSize, 16);
  header.writeUInt32LE(headerSize + hashTable.length, 20);
  header.writeUInt32LE(hashSize, 24);
  header.writeUInt32LE(count, 28);
  return Buffer.concat([header, hashTable, blocks, ...payloads]);
}
module.exports = {setupMpq, mapBytes};
