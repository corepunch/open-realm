/* Read ISO 9660/Joliet file extents from a browser Blob without loading the ISO. */
(function (root) {
  'use strict';

  const SECTOR_SIZE = 2048;
  const FIRST_DESCRIPTOR_SECTOR = 16;
  const MAX_DESCRIPTOR_SECTOR = 255;
  const PAGE_SIZE = 128 * SECTOR_SIZE;
  const MAX_DIRECTORY_DEPTH = 32;
  const MAX_DIRECTORIES = 10000;
  const MAX_ENTRIES = 100000;
  const MAX_PATH_LENGTH = 4096;
  const MAX_DIRECTORY_BYTES = 64 * 1024 * 1024;

  class ISO9660Error extends Error {
    constructor(message) {
      super(message);
      this.name = 'ISO9660Error';
    }
  }

  function checkCancelled(signal) {
    if (signal && signal.aborted) {
      const error = new Error('ISO scan cancelled');
      error.name = 'AbortError';
      throw error;
    }
  }

  // Slicing a Blob is a view of the selected byte range, not an in-memory ISO copy.
  async function readAt(file, offset, length, signal) {
    checkCancelled(signal);
    if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) ||
        offset < 0 || length < 0 || offset > file.size || length > file.size - offset) {
      throw new ISO9660Error('Read extends outside ISO image');
    }
    const bytes = new Uint8Array(await file.slice(offset, offset + length).arrayBuffer());
    checkCancelled(signal);
    if (bytes.length !== length) throw new ISO9660Error('Truncated ISO image');
    return bytes;
  }

  // ISO 9660 stores each numeric extent/size in both byte orders; disagreement
  // indicates a corrupt record, not an alternative byte order to guess at.
  function bothEndian16(data, offset) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const little = view.getUint16(offset, true);
    if (little !== view.getUint16(offset + 2, false)) {
      throw new ISO9660Error('Mismatched ISO 9660 numeric fields');
    }
    return little;
  }

  function bothEndian32(data, offset) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const little = view.getUint32(offset, true);
    if (little !== view.getUint32(offset + 4, false)) {
      throw new ISO9660Error('Mismatched ISO 9660 numeric fields');
    }
    return little;
  }

  // Native isoextract.c removes file versions and disallows filesystem
  // separators. Reject dot components and normalized collisions on traversal.
  function cleanComponent(name) {
    name = name.replace(/;[0-9]+$/, '');
    if (name.length > 1 && name.endsWith('.')) name = name.slice(0, -1);
    name = name.replace(/[\x00-\x1f/\\:]/g, '_');
    if (!name || name === '.' || name === '..') {
      throw new ISO9660Error('Unsafe ISO filename');
    }
    return name;
  }

  function decodeName(data, joliet) {
    if (!joliet) {
      // ISO 9660 identifiers are ASCII on standard discs. Latin-1 preserves
      // otherwise nonstandard single-byte identifiers without data loss.
      return cleanComponent(Array.from(data, byte => String.fromCharCode(byte)).join(''));
    }
    if (data.length % 2) throw new ISO9660Error('Odd-length Joliet filename');
    let name = '';
    for (let i = 0; i < data.length; i += 2) {
      const code = data[i] * 256 + data[i + 1];
      if (code >= 0xd800 && code <= 0xdfff) {
        throw new ISO9660Error('Unsupported surrogate in Joliet filename');
      }
      name += String.fromCharCode(code);
    }
    return cleanComponent(name);
  }

  // Parse a single self-contained record. Directory records cannot straddle
  // a logical block, even when the surrounding directory spans many blocks.
  function parseRecord(data, offset, available, joliet) {
    const length = data[offset];
    if (length < 34 || length > available) {
      throw new ISO9660Error('Invalid ISO directory record length');
    }
    const record = data.subarray(offset, offset + length);
    const nameLength = record[32];
    if (!nameLength || 33 + nameLength > length) {
      throw new ISO9660Error('Invalid ISO directory identifier');
    }
    const special = nameLength === 1 && (record[33] === 0 || record[33] === 1);
    const flags = record[25];
    if (flags & 0x80) {
      throw new ISO9660Error('Multi-extent ISO files are not supported');
    }
    if (record[26] || record[27]) {
      throw new ISO9660Error('Interleaved ISO files are not supported');
    }
    if (bothEndian16(record, 28) !== 1) {
      throw new ISO9660Error('Unsupported ISO volume sequence');
    }
    return {
      extent: bothEndian32(record, 2),
      size: bothEndian32(record, 10),
      xattrSectors: record[1],
      isDirectory: (flags & 2) !== 0,
      associated: (flags & 4) !== 0,
      special,
      name: special ? '' : decodeName(record.subarray(33, 33 + nameLength), joliet)
    };
  }

  // Directory and regular-file content begins after any extended-attribute
  // blocks. Check the complete extent before offering it to an importer.
  function contentRange(file, record) {
    const offset = (record.extent + record.xattrSectors) * SECTOR_SIZE;
    if (!Number.isSafeInteger(offset) || offset > file.size ||
        record.size > file.size - offset) {
      throw new ISO9660Error('ISO file extent extends outside image');
    }
    return offset;
  }

  function rootRecord(sector) {
    const record = parseRecord(sector, 156, SECTOR_SIZE - 156, false);
    if (!record.isDirectory || !record.special || record.associated) {
      throw new ISO9660Error('Invalid ISO root directory record');
    }
    return record;
  }

  function isJoliet(sector) {
    return sector[88] === 37 && sector[89] === 47 &&
        (sector[90] === 64 || sector[90] === 67 || sector[90] === 69);
  }

  class ISO9660Image {
    constructor(file, rootDirectory, joliet, signal) {
      this.file = file;
      this.joliet = joliet;
      this.sectorSize = SECTOR_SIZE;
      this._rootDirectory = rootDirectory;
      this._signal = signal;
      this._listing = null;
    }

    // Read only bounded directory pages; ordinary file contents remain on the
    // source Blob. The visited set also prevents malformed directory cycles.
    async _collectFiles() {
      const results = [];
      const seenDirectories = new Set();
      const seenPaths = new Set();
      const stack = [{record: this._rootDirectory, path: '', depth: 0}];
      while (stack.length) {
        checkCancelled(this._signal);
        const {record: directory, path, depth} = stack.pop();
        if (depth > MAX_DIRECTORY_DEPTH || seenDirectories.size >= MAX_DIRECTORIES) {
          throw new ISO9660Error('ISO directory traversal limit exceeded');
        }
        if (directory.size > MAX_DIRECTORY_BYTES) {
          throw new ISO9660Error('ISO directory is too large');
        }
        const directoryOffset = contentRange(this.file, directory);
        const key = directoryOffset + ':' + directory.size;
        if (seenDirectories.has(key)) {
          throw new ISO9660Error('Repeated or cyclic ISO directory extent');
        }
        seenDirectories.add(key);

        for (let pageStart = 0; pageStart < directory.size; pageStart += PAGE_SIZE) {
          const length = Math.min(PAGE_SIZE, directory.size - pageStart);
          const page = await readAt(this.file, directoryOffset + pageStart, length, this._signal);
          let pos = 0;
          while (pos < length) {
            const recordLength = page[pos];
            if (recordLength === 0) {
              // Zero padding lasts to the next logical sector boundary.
              pos = Math.min(length, (Math.floor(pos / SECTOR_SIZE) + 1) * SECTOR_SIZE);
              continue;
            }
            const remainingSector = SECTOR_SIZE - (pos % SECTOR_SIZE);
            if (recordLength > remainingSector) {
              throw new ISO9660Error('ISO directory record crosses sector boundary');
            }
            const entry = parseRecord(page, pos, Math.min(remainingSector, length - pos), this.joliet);
            pos += recordLength;
            if (entry.special || entry.associated) continue;
            const childPath = path ? path + '/' + entry.name : entry.name;
            if (childPath.length > MAX_PATH_LENGTH) {
              throw new ISO9660Error('ISO path is too long');
            }
            const folded = childPath.toLowerCase();
            if (seenPaths.has(folded)) {
              throw new ISO9660Error('Duplicate ISO path: ' + childPath);
            }
            seenPaths.add(folded);
            if (seenPaths.size > MAX_ENTRIES) {
              throw new ISO9660Error('ISO entry count limit exceeded');
            }
            const offset = contentRange(this.file, entry);
            if (entry.isDirectory) {
              stack.push({record: entry, path: childPath, depth: depth + 1});
            } else {
              results.push(Object.freeze({path: childPath, name: entry.name, offset, size: entry.size}));
            }
          }
        }
      }
      return Object.freeze(results);
    }

    // Returns frozen descriptors; consumers copy [offset, offset + size)
    // using the original Blob. Calling this again does not rescan the disc.
    async listFiles() {
      if (!this._listing) this._listing = this._collectFiles();
      return this._listing;
    }

    // A basename matches at any depth; a slash-qualified query matches only
    // the complete relative path. Matching is ASCII/Unicode case-insensitive.
    async findFiles(query) {
      if (typeof query !== 'string' || !query.length) {
        throw new TypeError('Expected an ISO filename or relative path');
      }
      const name = query.replace(/\\/g, '/').toLowerCase();
      const byPath = name.includes('/');
      return (await this.listFiles()).filter(entry =>
        (byPath ? entry.path : entry.name).toLowerCase() === name);
    }
  }

  // The primary volume descriptor is mandatory. Prefer a valid Joliet
  // supplementary descriptor for names, matching tools/isoextract.c.
  async function openISO(file, options = {}) {
    if (!file || !Number.isSafeInteger(file.size) || file.size < 0 ||
        typeof file.slice !== 'function') {
      throw new TypeError('Expected a File or Blob containing an ISO image');
    }
    const signal = options.signal;
    let primary = null;
    let supplementary = null;
    let terminated = false;
    for (let index = FIRST_DESCRIPTOR_SECTOR; index <= MAX_DESCRIPTOR_SECTOR; index++) {
      const sector = await readAt(file, index * SECTOR_SIZE, SECTOR_SIZE, signal);
      if (sector[1] !== 67 || sector[2] !== 68 || sector[3] !== 48 ||
          sector[4] !== 48 || sector[5] !== 49 || sector[6] !== 1) {
        throw new ISO9660Error('Not a supported ISO 9660 volume');
      }
      if (sector[0] === 255) {
        terminated = true;
        break;
      }
      if (sector[0] !== 1 && (sector[0] !== 2 || !isJoliet(sector))) continue;
      if (bothEndian16(sector, 128) !== SECTOR_SIZE) {
        throw new ISO9660Error('Unsupported ISO logical sector size');
      }
      const volumeSectors = bothEndian32(sector, 80);
      if (volumeSectors * SECTOR_SIZE > file.size) {
        throw new ISO9660Error('ISO volume is truncated');
      }
      const directory = rootRecord(sector);
      contentRange(file, directory);
      if (sector[0] === 1) primary = directory;
      else supplementary = directory;
    }
    if (!terminated) throw new ISO9660Error('ISO volume descriptor terminator missing');
    if (!primary) throw new ISO9660Error('ISO primary volume descriptor missing');
    return new ISO9660Image(file, supplementary || primary, !!supplementary, signal);
  }

  const api = Object.freeze({openISO, ISO9660Error});
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.ISO9660 = api;
})(globalThis);
