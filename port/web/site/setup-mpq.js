/* Read a *bounded, named* subset of original Warcraft III Setup.mpq files.
 * Do not attempt generic MPQ listing or execute installer code. No OPFS writes. */
(function (scope) {
  'use strict';
  const HASH_KEY = 0xC3AF3770;
  const BLOCK_KEY = 0xEC83B3A3;
  const FILE_EXISTS = 0x80000000;
  const COMPRESS = 0x200, ENCRYPTED = 0x10000, KEY_ADJUST = 0x20000;
  const SINGLE_UNIT = 0x1000000;
  const MAX_TABLE = 65536, MAX_MAP = 32 * 1024 * 1024;
  const MAX_TOTAL = 128 * 1024 * 1024;
  class SetupMPQError extends Error { constructor(message) { super(message); this.name = 'SetupMPQError'; } }
  function requireCondition(ok, message) { if (!ok) throw new SetupMPQError(message); }
  function aborted(signal) { if (signal && signal.aborted) throw new DOMException('Import cancelled', 'AbortError'); }
  const storm = new Uint32Array(0x500);
  let seed = 0x100001;
  for (let i = 0; i < 0x100; i++) for (let j = i; j < 0x500; j += 0x100) {
    seed = (seed * 125 + 3) % 0x2aaaab;
    const hi = (seed & 0xffff) << 16;
    seed = (seed * 125 + 3) % 0x2aaaab;
    storm[j] = (hi | (seed & 0xffff)) >>> 0;
  }
  function hash(name, kind) {
    let a = 0x7fed7fed, b = 0xeeeeeeee;
    for (const char of name.replace(/\//g, '\\').toUpperCase()) {
      const c = char.charCodeAt(0);
      requireCondition(c <= 0x7f, 'Unsupported non-ASCII installer filename');
      a = (storm[(kind << 8) + c] ^ ((a + b) >>> 0)) >>> 0;
      b = (c + a + b + (b << 5) + 3) >>> 0;
    }
    return a;
  }
  function decrypt(bytes, key) {
    requireCondition(bytes.byteLength % 4 === 0, 'Unaligned encrypted MPQ table');
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    let b = 0xeeeeeeee;
    for (let pos = 0; pos < bytes.byteLength; pos += 4) {
      const base = (b + storm[0x400 + (key & 255)]) >>> 0;
      const value = (view.getUint32(pos, true) ^ ((key + base) >>> 0)) >>> 0;
      view.setUint32(pos, value, true);
      key = (((~key << 21) + 0x11111111) | (key >>> 11)) >>> 0;
      b = (value + base + (base << 5) + 3) >>> 0;
    }
    return bytes;
  }
  async function at(blob, offset, length, signal) {
    aborted(signal);
    requireCondition(Number.isSafeInteger(offset) && Number.isSafeInteger(length) &&
      offset >= 0 && length >= 0 && offset + length <= blob.size,
      'MPQ data range exceeds Setup.mpq');
    const bytes = new Uint8Array(await blob.slice(offset, offset + length).arrayBuffer());
    aborted(signal);
    requireCondition(bytes.length === length, 'Truncated Setup.mpq');
    return bytes;
  }
  function word(bytes, pos) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(pos, true); }
  function isMap(bytes) {
    if (bytes.length < 32) return false;
    // Classic maps optionally contain an HM3W application header before MPQ.
    for (let i = 0; i + 4 <= Math.min(bytes.length, 1024); i++)
      if (bytes[i] === 77 && bytes[i + 1] === 80 && bytes[i + 2] === 81 && bytes[i + 3] === 26) return true;
    return false;
  }
  async function inflate(compressed, expected) {
    requireCondition(compressed.length > 1 && compressed[0] === 2,
      'Unsupported Setup.mpq map compression (expected zlib)');
    requireCondition(typeof DecompressionStream === 'function',
      'This browser does not support zlib DecompressionStream');
    const stream = new Blob([compressed.subarray(1)]).stream().pipeThrough(new DecompressionStream('deflate'));
    const reader = stream.getReader();
    const chunks = [];
    let length = 0;
    try {
      while (true) {
        const {done, value} = await reader.read();
        if (done) break;
        length += value.length;
        requireCondition(length <= expected, 'MPQ compressed map expanded beyond expected size');
        chunks.push(value);
      }
    } catch (err) { await reader.cancel().catch(() => {}); throw err; }
    requireCondition(length === expected, 'MPQ compressed map has incorrect size');
    const out = new Uint8Array(length);
    let pos = 0;
    for (const chunk of chunks) { out.set(chunk, pos); pos += chunk.length; }
    return out;
  }
  async function decodeSector(bytes, size) {
    if (bytes.length === size) return bytes;
    return inflate(bytes, size);
  }
  class Reader {
    constructor(blob, base, length, sectorSize, hashes, blocks, signal) {
      Object.assign(this, {blob, base, length, sectorSize, hashes, blocks, signal});
    }
    find(name) {
      const a = hash(name, 1), b = hash(name, 2);
      const mask = this.hashes.length - 1;
      for (let i = 0, index = hash(name, 0) & mask; i < this.hashes.length; i++, index = (index + 1) & mask) {
        const item = this.hashes[index];
        if (item.index === 0xffffffff) break;
        if (item.index !== 0xfffffffe && item.a === a && item.b === b) {
          requireCondition(item.index < this.blocks.length, 'Invalid Setup.mpq block index');
          return this.blocks[item.index];
        }
      }
      return null;
    }
    has(name) { return this.find(name) !== null; }
    async read(name) {
      aborted(this.signal);
      const block = this.find(name);
      if (!block) return null;
      requireCondition((block.flags & FILE_EXISTS) !== 0 && block.size > 0 && block.size <= MAX_MAP,
        'Invalid or oversized installer map: ' + name);
      requireCondition(!(block.flags & 0x100) && !(block.flags & 0x4000000),
        'Unsupported MPQ implode/checksum map entry: ' + name);
      requireCondition(block.offset + block.packed <= this.length && block.packed > 0,
        'Installer map exceeds archive: ' + name);
      const read = (offset, size) => at(this.blob, this.base + block.offset + offset, size, this.signal);
      let key = 0;
      if (block.flags & ENCRYPTED) {
        key = hash(name.split(/[\\/]/).pop(), 3);
        if (block.flags & KEY_ADJUST) key = (((key + block.offset) >>> 0) ^ block.size) >>> 0;
      }
      let out;
      if (!(block.flags & COMPRESS)) {
        requireCondition(block.packed >= block.size, 'Short uncompressed installer map');
        out = await read(0, block.size);
        if (block.flags & ENCRYPTED) decrypt(out.subarray(0, out.length & ~3), key);
      } else if (block.flags & SINGLE_UNIT) {
        let packed = await read(0, block.packed);
        if (block.flags & ENCRYPTED) {
          const aligned = packed.length & ~3;
          decrypt(packed.subarray(0, aligned), key);
        }
        out = await decodeSector(packed, block.size);
      } else {
        const sectors = Math.ceil(block.size / this.sectorSize);
        const tableLength = (sectors + 1) * 4;
        requireCondition(tableLength <= block.packed && sectors <= 8192,
          'Invalid Setup.mpq sector table');
        let table = await read(0, tableLength);
        if (block.flags & ENCRYPTED) decrypt(table, (key - 1) >>> 0);
        const offsets = Array.from({length: sectors + 1}, (_, i) => word(table, i * 4));
        requireCondition(offsets[0] >= tableLength && offsets.at(-1) <= block.packed,
          'Invalid Setup.mpq sector offsets');
        out = new Uint8Array(block.size);
        for (let i = 0; i < sectors; ++i) {
          aborted(this.signal);
          const start = offsets[i], end = offsets[i + 1];
          requireCondition(start <= end && end <= block.packed, 'Invalid Setup.mpq sector order');
          const count = Math.min(this.sectorSize, block.size - i * this.sectorSize);
          let chunk = await read(start, end - start);
          if (block.flags & ENCRYPTED) decrypt(chunk.subarray(0, chunk.length & ~3), (key + i) >>> 0);
          out.set(await decodeSector(chunk, count), i * this.sectorSize);
        }
      }
      requireCondition(isMap(out), 'Installer entry is not an MPQ map: ' + name);
      return new Blob([out]);
    }
  }
  async function open(blob, {signal} = {}) {
    requireCondition(blob && Number.isSafeInteger(blob.size) && typeof blob.slice === 'function',
      'Expected a Setup.mpq Blob');
    const probe = await at(blob, 0, Math.min(1024, blob.size), signal);
    let base = -1;
    for (let i = 0; i + 32 <= probe.length; ++i) {
      if (probe[i] === 77 && probe[i + 1] === 80 && probe[i + 2] === 81 && probe[i + 3] === 26) { base = i; break; }
    }
    requireCondition(base >= 0, 'Missing Setup.mpq header');
    const header = await at(blob, base, 32, signal);
    const archiveSize = word(header, 8), version = header[12] | (header[13] << 8);
    const shift = header[14] | (header[15] << 8);
    const hpos = word(header, 16), bpos = word(header, 20);
    const hcount = word(header, 24), bcount = word(header, 28);
    requireCondition((version === 0 || version === 1) && shift <= 15,
      'Unsupported Setup.mpq version or sector size');
    requireCondition(archiveSize >= 32 && base + archiveSize <= blob.size &&
      hcount > 0 && hcount <= MAX_TABLE && (hcount & (hcount - 1)) === 0 &&
      bcount > 0 && bcount <= MAX_TABLE, 'Invalid Setup.mpq tables');
    requireCondition(hpos + hcount * 16 <= archiveSize && bpos + bcount * 16 <= archiveSize,
      'Setup.mpq tables exceed archive');
    const hbytes = decrypt(await at(blob, base + hpos, hcount * 16, signal), HASH_KEY);
    const bbytes = decrypt(await at(blob, base + bpos, bcount * 16, signal), BLOCK_KEY);
    const hashes = Array.from({length:hcount}, (_, i) => ({
      a:word(hbytes, i * 16), b:word(hbytes, i * 16 + 4), index:word(hbytes, i * 16 + 12)
    }));
    const blocks = Array.from({length:bcount}, (_, i) => ({
      offset:word(bbytes, i * 16), packed:word(bbytes, i * 16 + 4),
      size:word(bbytes, i * 16 + 8), flags:word(bbytes, i * 16 + 12)
    }));
    return new Reader(blob, base, archiveSize, 512 << shift, hashes, blocks, signal);
  }
  // One observed original RoC disc layout (Polish retail release). These
  // installer-generated names are not universal across discs/patch levels.
  // Only use this mapping when the COMPLETE file set is present.
  const mapNames = [
    '(10)DustwallowKeys','(12)DivideAndConquer','(12)IceCrown','(2)BootyBay',
    '(2)Harrow','(2)HillsOfGlory','(2)OgreMound','(2)PlunderIsle',
    '(3)Forestwalk','(3)IsleOfDread','(3)Nighthaven',
    '(4)Adrenaline','(4)Borderlands','(4)Deadwaterdrop','(4)DragonMountain',
    '(4)Duskwood','(4)Frostsabre','(4)HarvestMoon','(4)Legends',
    '(4)LostTemple','(4)MysticIsles','(4)TranquilPaths','(5)TheGlaive',
    '(6)DarkForest','(6)DragonFire','(6)DrywaterGulch','(6)GnollWood',
    '(6)Moonglade','(6)ScorchedBasin','(6)Stromguarde','(6)SwampOfSorrows',
    '(6)TimbermawHold','(8)Battleground','(8)BlastedLands',
    '(8)BloodvenomFalls','(8)GardenOfWar','(8)GolemsInTheMist',
    '(8)PetrifiedForest','(8)Plaguelands','(8)PlainsOfSnow',
    '(8)TheCrucible','(9)Riverrun',
    'Scenario/(1)TheDeathSheep','Scenario/(4)WarChasers'
  ];
  const knownMaps = Object.freeze(mapNames.map((name, i) => Object.freeze({
    member:'File' + String(i + 69).padStart(8, '0') + '.w3x',
    path:'Maps/' + name + '.w3m'
  })));
  async function extractKnownMaps(blob, {signal} = {}) {
    const archive = await open(blob, {signal});
    if (!knownMaps.every(map => archive.has(map.member))) return null;
    const results = [];
    let total = 0;
    for (const map of knownMaps) {
      aborted(signal);
      const data = await archive.read(map.member);
      total += data.size;
      requireCondition(total <= MAX_TOTAL, 'Installer map data exceeds safety limit');
      results.push({name:map.path, blob:data, size:data.size});
    }
    return results;
  }
  scope.SetupMPQ = Object.freeze({open, extractKnownMaps, knownMaps, SetupMPQError});
})(globalThis);
