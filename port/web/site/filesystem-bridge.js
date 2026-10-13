/* Validate the active imported archive generation before native startup.
 * The game reads directly from the same OPFS root via WasmFS at /opfs.
 * No copies into MEMFS, and no legacy IDBFS writes or migrations. */
(function () {
  'use strict';
  const ROOT = '/opfs/openrealm/game-data/wc3';
  const MANIFEST_PATH = ['openrealm', 'game-data', 'wc3'];
  function status(message) {
    const node = document.getElementById('status');
    if (node) node.textContent = message;
  }
  function validName(name) {
    return typeof name === 'string' &&
      /^[a-z0-9][a-z0-9 _().-]{0,200}\.(mpq|w3m|w3x)$/i.test(name);
  }
  async function validate() {
    if (!navigator.storage || !navigator.storage.getDirectory)
      throw new Error('Browser-local OPFS storage is unavailable');
    let dir = await navigator.storage.getDirectory();
    for (const segment of MANIFEST_PATH)
      dir = await dir.getDirectoryHandle(segment);
    const text = await (await (await dir.getFileHandle('installation.json')).getFile()).text();
    const manifest = JSON.parse(text);
    if (!manifest || ![1, 2].includes(manifest.version) ||
        !Array.isArray(manifest.files) || !manifest.files.length || manifest.files.length > 512)
      throw new Error('Unsupported installation manifest');
    let archiveDir = dir;
    let path = ROOT;
    if (manifest.version === 2) {
      if (typeof manifest.generation !== 'string' ||
          !/^install-[a-z0-9]+-[a-z0-9]{1,16}$/.test(manifest.generation))
        throw new Error('Invalid installation generation');
      archiveDir = await dir.getDirectoryHandle(manifest.generation);
      path += '/' + manifest.generation;
    }
    const seen = new Set();
    let total = 0;
    for (const entry of manifest.files) {
      if (!entry || !validName(entry.name) || !Number.isSafeInteger(entry.size) || entry.size < 32)
        throw new Error('Invalid archive manifest entry');
      const key = entry.name.toLowerCase();
      if (seen.has(key)) throw new Error('Duplicate archive: ' + entry.name);
      seen.add(key);
      const file = await (await archiveDir.getFileHandle(entry.name)).getFile();
      if (file.size !== entry.size) throw new Error('Archive size changed: ' + entry.name);
      const probe = new Uint8Array(await file.slice(0, 1024).arrayBuffer());
      let found = false;
      for (let i = 0; i + 32 <= probe.length; i++) {
        if (probe[i] === 77 && probe[i + 1] === 80 &&
            probe[i + 2] === 81 && probe[i + 3] === 26) { found = true; break; }
      }
      if (!found) throw new Error('Invalid MPQ archive: ' + entry.name);
      total += file.size;
      if (!Number.isSafeInteger(total)) throw new Error('Installation size overflow');
    }
    if (!seen.has('war3.mpq') || manifest.bytes !== total)
      throw new Error('War3.mpq missing or installation size mismatch');
    return path;
  }
  Module.preRun = Module.preRun || [];
  Module.preRun.push(function () {
    const dependency = 'openrealm-opfs-validate';
    Module.addRunDependency(dependency);
    validate().then(path => {
      Module.openrealmDataPath = path;
      status('Validated browser installation; starting engine…');
    }).catch(error => {
      const message = error && error.message || String(error);
      status('Cannot start OpenRealm: ' + message);
      // Abort rather than continuing with a stale or missing -data argument.
      // Keep the run dependency pending on validation failure: never enter C main.
    });
  });
  // The previous IDBFS save directory is intentionally left untouched.
  // A follow-up migration is required before claiming old saves are accessible.
})();
