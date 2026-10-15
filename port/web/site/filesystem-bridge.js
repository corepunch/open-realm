/* Validate the active imported archive generation before native startup.
 * The game reads directly from the same OPFS root via WasmFS at /opfs.
 * No copies into MEMFS, and no legacy IDBFS writes or migrations. */
(function () {
  'use strict';
  const ROOT = '/opfs/openrealm/game-data/wc3';
  const MANIFEST_PATH = ['openrealm', 'game-data', 'wc3'];
  const COMMIT_LOCK = 'openrealm-wc3-installation';
  const GENERATION_LOCK = 'openrealm-wc3-generation:';
  const MODE_KEY = 'openrealm-wc3-browser-mode';
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
    try {
      for (const segment of MANIFEST_PATH)
        dir = await dir.getDirectoryHandle(segment);
    } catch (error) {
      if (error && error.name === 'NotFoundError')
        throw new Error('No Warcraft III files are installed in this browser. Import the official game MPQs first.');
      throw error;
    }
    const text = await (await (await dir.getFileHandle('installation.json')).getFile()).text();
    const manifest = JSON.parse(text);
    if (!manifest || ![1, 2, 3].includes(manifest.version) ||
        !Array.isArray(manifest.files) || !manifest.files.length || manifest.files.length > 512)
      throw new Error('Unsupported installation manifest');
    // v1-v3 manifests without edition came from older retail-only importers.
    // The label is informational, but reject contradictory new metadata.
    if (manifest.edition !== undefined &&
        manifest.edition !== 'demo' && manifest.edition !== 'retail')
      throw new Error('Invalid installation edition');
    if (manifest.edition === 'demo' && (manifest.version !== 3 ||
        manifest.completeness !== 'demo' ||
        manifest.files.some(entry => entry && /^war3x(local)?\.mpq$/i.test(entry.name))))
      throw new Error('Invalid demo installation manifest');
    let archiveDir = dir;
    let path = ROOT;
    if (manifest.version >= 2) {
      if (typeof manifest.generation !== 'string' ||
          !/^install-[a-z0-9]+-[a-z0-9]{1,16}$/.test(manifest.generation))
        throw new Error('Invalid installation generation');
      archiveDir = await dir.getDirectoryHandle(manifest.generation);
      path += '/' + manifest.generation;
    }
    const seen = new Set();
    let total = 0;
    for (const entry of manifest.files) {
      const parts = entry && typeof entry.name === 'string' ? entry.name.split('/') : [];
      const nestedMap = manifest.version === 3 && parts.length >= 2 &&
        parts.length <= 8 && parts[0] === 'Maps' &&
        parts.slice(1, -1).every(segment => /^[a-z0-9][a-z0-9 _().-]{0,100}$/i.test(segment) &&
          segment !== '.' && segment !== '..') &&
        /^[a-z0-9(][a-z0-9 _().-]{0,200}\.w3[mx]$/i.test(parts.at(-1));
      if (!entry || !(validName(entry.name) || nestedMap) ||
          !Number.isSafeInteger(entry.size) || entry.size < 32)
        throw new Error('Invalid archive manifest entry');
      const key = entry.name.toLowerCase();
      if (seen.has(key)) throw new Error('Duplicate archive: ' + entry.name);
      seen.add(key);
      let parent = archiveDir;
      if (nestedMap) for (const part of parts.slice(0, -1))
        parent = await parent.getDirectoryHandle(part);
      const file = await (await parent.getFileHandle(parts.at(-1))).getFile();
      if (file.size !== entry.size) throw new Error('Archive size changed: ' + entry.name);
      if (!window.MPQHeader) throw new Error('MPQ validation module is missing');
      try {
        const header = await window.MPQHeader.validate(file);
        if (/\.mpq$/i.test(entry.name) && file.size - header.offset - header.archiveSize > 4096)
          throw new Error('MPQ has excessive trailing data beyond its declared archive size');
      }
      catch (err) { throw new Error('Invalid MPQ archive ' + entry.name + ': ' + err.message); }
      total += file.size;
      if (!Number.isSafeInteger(total)) throw new Error('Installation size overflow');
    }
    if (!seen.has('war3.mpq') || manifest.bytes !== total)
      throw new Error('War3.mpq missing or installation size mismatch');
    return {path, generation:manifest.version >= 2 ? manifest.generation : 'legacy',
      edition:manifest.edition === 'demo' ? 'demo' : 'retail',
      hasExpansion:seen.has('war3x.mpq')};
  }
  // A shared commit lock protects the manifest-to-generation transition.
  // After reading it, hold a shared generation lease for this tab's lifetime,
  // so another tab cannot delete archives that the engine has open. Web Locks
  // are released automatically when the page is closed or navigated away.
  async function begin() {
    if (!navigator.locks || typeof navigator.locks.request !== 'function')
      throw new Error('Web Locks API is required to protect installed game data');
    await navigator.locks.request(COMMIT_LOCK, {mode:'shared'}, async () => {
      const installation = await validate();
      const selector = document.getElementById('game-edition');
      const requested = sessionStorage.getItem(MODE_KEY);
      const mode = requested === 'tft' && installation.hasExpansion &&
        installation.edition !== 'demo' ? 'tft' : 'roc';
      if (selector) {
        selector.disabled = installation.edition === 'demo' || !installation.hasExpansion;
        selector.value = mode;
      }
      if (requested !== mode) sessionStorage.setItem(MODE_KEY, mode);
      await new Promise((resolve, reject) => {
        navigator.locks.request(GENERATION_LOCK + installation.generation,
          {mode:'shared'}, async () => {
            try {
              status(installation.edition === 'demo'
                ? 'Warcraft III Demo installation — limited content; starting experimental engine…'
                : 'Validated browser installation; starting engine…');
              const started = await window.openrealmStartEngine([
                '+openrealm-data=' + installation.path,
                '+set', 'fs_expansion', mode === 'tft' ? '1' : '0']);
              if (!started) {
                const error = new Error('Browser support or cross-origin isolation is unavailable.');
                error.code = 'OPENREALM_STARTUP_UNAVAILABLE';
                reject(error);
                return;
              }
              if (window.openrealmGamePresentation) window.openrealmGamePresentation('playing');
              resolve(); // Release commit lock after acquiring the lease.
            } catch (err) { reject(err); return; }
            // The engine may access WasmFS indefinitely. Keep this lease until
            // the document is unloaded; never rely on a guessed timeout.
            await new Promise(() => {});
          }).catch(reject);
      });
    });
  }
  // Changing edition requires a reload: the existing engine owns the canvas
  // and its WasmFS lifetime cannot be safely reinitialized in the same page.
  const selector = document.getElementById('game-edition');
  if (selector) selector.addEventListener('change', () => {
    if (selector.disabled || !['roc', 'tft'].includes(selector.value)) return;
    sessionStorage.setItem(MODE_KEY, selector.value);
    location.reload();
  });
  // Installer's removal-only reload must never re-acquire the game-data lease.
  if (sessionStorage.getItem('openrealm-wc3-remove-after-reload') !== '1')
    begin().catch(error => {
    const message = error && error.message || String(error);
    status(error && error.code === 'OPENREALM_STARTUP_UNAVAILABLE'
      ? 'Cannot start OpenRealm: ' + message
      : 'Cannot start OpenRealm: ' + message + ' Import the game files above, then reload this page.');
  });
  // The previous IDBFS save directory is intentionally left untouched.
  // A follow-up migration is required before claiming old saves are accessible.
})();
