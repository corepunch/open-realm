/* Bounded structural MPQ validation shared by installation and launch.
 * This does not decrypt archive payloads or prove that game assets are complete. */
(function (scope) {
  'use strict';
  const MAX_PREFIX = 1024;
  function invalid(message) { throw new Error('Invalid MPQ archive: ' + message); }
  function structuralHeader(bytes, base, fileSize) {
    if (base + 32 > bytes.length) return null;
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const get32 = offset => view.getUint32(base + offset, true);
    const get16 = offset => view.getUint16(base + offset, true);
    const headerSize = get32(4), archiveSize = get32(8);
    const version = get16(12), sectorShift = get16(14);
    const hashOffset = get32(16), blockOffset = get32(20);
    const hashCount = get32(24), blockCount = get32(28);
    if ((version !== 0 && version !== 1) ||
        headerSize < (version === 1 ? 44 : 32) || headerSize > 1024 ||
        archiveSize < headerSize || base + archiveSize > fileSize ||
        sectorShift > 15 || hashCount === 0 || hashCount > 1048576 ||
        (hashCount & (hashCount - 1)) !== 0 ||
        blockCount === 0 || blockCount > 1048576 ||
        hashOffset < headerSize || blockOffset < headerSize ||
        hashOffset + hashCount * 16 > archiveSize ||
        blockOffset + blockCount * 16 > archiveSize) return null;
    // The prefix is normally zero for game archives; Warcraft III map files
    // can place an HM3W map header before their actual MPQ header.
    return Object.freeze({offset:base, archiveSize, headerSize, version,
      hashOffset, blockOffset, hashCount, blockCount});
  }
  async function validate(blob, {signal} = {}) {
    if (!blob || !Number.isSafeInteger(blob.size) || blob.size < 32 ||
        typeof blob.slice !== 'function') invalid('missing or truncated file');
    if (signal && signal.aborted) throw new DOMException('Import cancelled', 'AbortError');
    const prefix = new Uint8Array(await blob.slice(0, Math.min(blob.size, MAX_PREFIX + 32)).arrayBuffer());
    if (signal && signal.aborted) throw new DOMException('Import cancelled', 'AbortError');
    for (let base = 0; base <= MAX_PREFIX && base + 32 <= prefix.length; ++base) {
      if (prefix[base] !== 0x4d || prefix[base + 1] !== 0x50 ||
          prefix[base + 2] !== 0x51 || prefix[base + 3] !== 0x1a) continue;
      const header = structuralHeader(prefix, base, blob.size);
      if (header) return header;
    }
    invalid('no structurally valid header or bounded hash/block tables');
  }
  const api = Object.freeze({validate});
  scope.MPQHeader = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(globalThis);
