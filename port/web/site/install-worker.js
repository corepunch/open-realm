/* Game data never leaves the user's origin. This worker owns writes to OPFS. */
'use strict';
const ROOT = 'openrealm';
const GAME = 'game-data';
const WC3 = 'wc3';
const META = 'installation.json';
const CHUNK = 1024 * 1024;
const MAX_INPUT_FILES = 512; // Match filesystem-bridge.js manifest limit.
const MAX_ISO_IMAGES = 8; // Limit expensive full-directory scans per import.
const COMMIT_LOCK = 'openrealm-wc3-installation';
const GENERATION_LOCK = 'openrealm-wc3-generation:';
// Only recognized game archives are mountable. Setup.mpq is an installer
// container: recover known map payloads but never mount the container itself.
const DISC_ARCHIVES = Object.freeze({
  'war3.mpq': 'War3.mpq',
  'war3local.mpq': 'War3Local.mpq',
  'war3x.mpq': 'War3x.mpq',
  'war3xlocal.mpq': 'War3xLocal.mpq',
  'war3patch.mpq': 'War3Patch.mpq'
});
let cancelled = false;
let active = false;
let scanController = null;
const allowed = name => /\.(mpq|w3m|w3x)$/i.test(name);
const isoName = name => /\.iso$/i.test(name);
const cleanName = name => typeof name === 'string' && /^[a-z0-9][a-z0-9 _().-]{0,200}$/i.test(name) && name !== '.' && name !== '..';
function checkCancelled() {
  if (cancelled) throw new Error('Import cancelled');
}
// Convert direct uploads or ISO extents into the same Blob-backed contract.
async function selectSources(files) {
  if (!Array.isArray(files) || !files.length || files.length > MAX_INPUT_FILES) {
    throw new Error('Select 1-512 MPQ, W3M, W3X or ISO files');
  }
  const images = files.filter(f => f && isoName(f.name));
  if (images.length > MAX_ISO_IMAGES) {
    throw new Error('Too many ISO images in one import (maximum 8)');
  }
  if (files.some(f => !f || !cleanName(f.name) ||
                       (!isoName(f.name) && !allowed(f.name)) ||
                       !Number.isSafeInteger(f.size) || f.size < 0 ||
                       typeof f.slice !== 'function')) {
    throw new Error('Select only ISO, MPQ, W3M or W3X files with safe filenames');
  }

  // Resolve all inputs before writing anything. A Reign of Chaos ISO and a
  // Frozen Throne ISO (or direct MPQs) can supply complementary archives.
  // Do not silently choose between two sources with the same archive name.
  const sources = [];
  const setups = [];
  const names = new Map();
  let demoDiscCount = 0;
  function add(name, blob, source) {
    const key = name.toLowerCase();
    if (names.has(key)) {
      throw new Error('Duplicate archive ' + name + ' from ' + source +
          ' (already provided by ' + names.get(key) + ')');
    }
    names.set(key, source);
    sources.push({name, size:blob.size, blob, source});
    if (sources.length > MAX_INPUT_FILES) {
      throw new Error('Too many game files in one installation');
    }
  }

  for (const file of files) {
    checkCancelled();
    if (!isoName(file.name)) {
      if (/^setup\.mpq$/i.test(file.name)) setups.push({blob:file, source:file.name});
      else add(file.name, file, file.name);
      continue;
    }
    postMessage({type:'phase', phase:'scanning', source:file.name});
    // MPQ-only installs must still function when an older site build does
    // not ship the ISO module. ISO scanning never materializes a full image.
    if (!globalThis.ISO9660) importScripts('iso9660.js');
    const image = await ISO9660.openISO(file, {signal: scanController.signal});
    const entries = await image.listFiles();
    checkCancelled();
    // Detect the documented September 2002 demo by its disc contents, not
    // its ISO name or MPQ size. Other demo layouts remain unverified.
    const discNames = new Set(entries.map(entry => entry.path.toLowerCase()));
    const isDemo = discNames.has('war3demo.ico') && discNames.has('scdemo.exe');
    if ((isDemo && (images.length !== 1 ||
        files.some(input => !isoName(input.name) && /\.mpq$/i.test(input.name)))) ||
        (demoDiscCount && !isDemo)) {
      throw new Error('Warcraft III Demo discs must be imported separately from ' +
        'retail ISOs and standalone MPQs');
    }
    if (isDemo) {
      demoDiscCount++;
      postMessage({type:'notice', message:'Warcraft III Demo disc detected in ' +
        file.name + '. Demo content is limited and is not a retail installation.'});
    }
    const onDisc = new Set();
    // Never apply RoC's anonymous FileNN map mapping to a TFT-only disc.
    const baseDisc = entries.some(entry => entry.name.toLowerCase() === 'war3.mpq');
    for (const entry of entries) {
      checkCancelled();
      const name = DISC_ARCHIVES[entry.name.toLowerCase()];
      if (!name) {
        if (entry.name.toLowerCase() === 'setup.mpq' && !isDemo)
          setups.push({blob:file.slice(entry.offset, entry.offset + entry.size), source:file.name, roc:baseDisc});
        // The documented demo exposes three standalone melee maps in
        // RetailMaps/. Store them in the regular Maps/ search directory.
        if (isDemo && entry.path.toLowerCase().startsWith('retailmaps/') &&
            entry.path.slice('retailmaps/'.length).indexOf('/') === -1 &&
            /^[a-z0-9(][a-z0-9 _().-]{0,200}\.w3[mx]$/i.test(entry.name)) {
          add('Maps/' + entry.name, file.slice(entry.offset, entry.offset + entry.size), file.name);
        }
        continue;
      }
      const key = name.toLowerCase();
      if (onDisc.has(key)) throw new Error('Ambiguous archive in ISO: ' + name);
      onDisc.add(key);
      // Validated extents are views into the original Blob, not ISO copies.
      add(name, file.slice(entry.offset, entry.offset + entry.size), file.name);
    }
  }
  // Demo and retail assets have different coverage and must not masquerade
  // as a combined retail installation. Standalone maps are still permitted.
  if (demoDiscCount && (demoDiscCount !== 1 || images.length !== 1 ||
      files.some(file => !isoName(file.name) && /\.mpq$/i.test(file.name)))) {
    throw new Error('Warcraft III Demo discs must be imported separately from ' +
      'retail ISOs and standalone MPQs');
  }
  // Never infer installer File000000NN names from an unknown release. An
  // observed retail RoC layout is supported only if its entire map set is
  // present; other Setup.mpq containers are skipped with a visible notice.
  if (setups.length) {
    postMessage({type:'phase', phase:'extracting'});
    if (!globalThis.SetupMPQ) importScripts('setup-mpq.js');
    for (const setup of setups) {
      checkCancelled();
      // Discover *all* intact nested game archives. Named entries include
      // ordinary War3/War3Local/War3Patch archives; anonymous entries require
      // distinct internal path fingerprints before receiving a game filename.
      const discovered = await SetupMPQ.extractGameArchives(setup.blob,
        {signal:scanController.signal});
      for (const entry of discovered.archives)
        add(entry.name, entry.blob, setup.source + ' / Setup.mpq (' + entry.method + ')');
      if (discovered.unidentified)
        postMessage({type:'notice', message:discovered.unidentified +
          ' embedded MPQ(s) in ' + setup.source + ' / Setup.mpq could not be ' +
          'identified by contents and were not installed.'});
      if (setup.roc === false) {
        if (!discovered.archives.length)
          postMessage({type:'notice', message:'No named expansion or identifiable embedded game MPQs in ' +
            setup.source + ' / Setup.mpq; unsupported layouts require further investigation.'});
        continue;
      }
      const maps = await SetupMPQ.extractKnownMaps(setup.blob, {signal:scanController.signal});
      if (!maps) {
        postMessage({type:'notice', message:'Unknown Setup.mpq layout in ' + setup.source +
          '; installer maps were not imported. Game archives are still usable.'});
        continue;
      }
      for (const map of maps) add(map.name, map.blob, setup.source + ' / Setup.mpq');
    }
  }
  // A standalone TFT disc is a valid *input*, but not a complete install.
  // Check the union of all sources, not each ISO separately.
  if (names.has('war3xlocal.mpq') && !names.has('war3x.mpq'))
    throw new Error('War3xLocal.mpq requires War3x.mpq in the same installation');
  if (!names.has('war3.mpq')) {
    throw new Error('War3.mpq missing; select the Reign of Chaos base disc or MPQ alongside expansion data');
  }
  // An explicitly selected TFT Setup.mpq must not quietly produce a RoC-only
  // installation and report success. Retain the old generation instead.
  if (setups.some(setup => setup.roc === false) && !names.has('war3x.mpq')) {
    throw new Error('Could not recover War3x.mpq from the Frozen Throne Setup.mpq. ' +
      'Anonymous MPQs must match internal game-data fingerprints; provide ' +
      'installed expansion MPQs if this disc layout is unsupported.');
  }
  return {sources, edition: demoDiscCount ? 'demo' : 'retail'};
}
// Best-effort quota check: estimates are advisory, and browsers can evict
// origin data or deny later writes. The actual copy still handles failures.
async function checkAvailableStorage(bytes) {
  if (typeof navigator.storage.estimate !== 'function') return;
  let estimate;
  try { estimate = await navigator.storage.estimate(); }
  catch (_) { return; } // A refused estimate must not disable imports.
  checkCancelled();
  const {usage, quota} = estimate || {};
  if (!Number.isFinite(quota) || !Number.isFinite(usage) ||
      quota < 0 || usage < 0 || usage > quota) return;
  if (bytes > quota - usage) {
    throw new Error('Not enough estimated browser storage for ' + bytes +
        ' bytes of game data; free storage and retry');
  }
}
async function directory(create) {
  const origin = await navigator.storage.getDirectory();
  const root = await origin.getDirectoryHandle(ROOT, {create});
  const game = await root.getDirectoryHandle(GAME, {create});
  return game.getDirectoryHandle(WC3, {create});
}
async function metadata(dir) {
  try {
    const file = await (await dir.getFileHandle(META)).getFile();
    return JSON.parse(await file.text());
  } catch (err) { if (err.name === 'NotFoundError') return null; throw err; }
}
// Require a structurally valid MPQ envelope and in-bounds tables. This is
// stronger than magic-byte checking but does not prove every payload is intact.
async function checkMpqHeader(file) {
  if (!globalThis.MPQHeader) importScripts('mpq-header.js');
  try {
    const header = await MPQHeader.validate(file.blob, {signal:scanController.signal});
    // Classic MPQs may carry short signatures/alignment trailers. Reject
    // implausible excess data, but do not demand byte-for-byte header length.
    if (/\.mpq$/i.test(file.name) && file.size - header.offset - header.archiveSize > 4096)
      throw new Error('MPQ has excessive trailing data beyond its declared archive size');
  }
  catch (err) {
    if (err.name === 'AbortError') throw err;
    throw new Error('Invalid MPQ archive ' + file.name + ': ' + err.message);
  }
  checkCancelled();
}

// The writer and the launcher take opposite modes of this lock. Any tab
// starting the engine must finish acquiring its generation lease before a
// writer is allowed to commit and prune an older generation.
async function withCommitLock(action) {
  if (!navigator.locks || typeof navigator.locks.request !== 'function')
    throw new Error('The Web Locks API is required for safe browser installation');
  return navigator.locks.request(COMMIT_LOCK, {mode:'exclusive', ifAvailable:true}, async lock => {
    if (!lock) throw new Error('Another browser tab is installing or starting Warcraft III');
    return action();
  });
}
const isGeneration = name => /^install-[a-z0-9]+-[a-z0-9]{1,16}$/.test(name);
async function generations(dir) {
  const result = [];
  for await (const [name, entry] of dir.entries())
    if (entry.kind === 'directory' && isGeneration(name)) result.push(name);
  return result;
}
async function withGenerationLocks(names, action, index = 0) {
  if (index === names.length) return action();
  return navigator.locks.request(GENERATION_LOCK + names[index],
    {mode:'exclusive', ifAvailable:true}, async lock => {
      if (!lock) throw new Error('Close other Warcraft III game tabs before removing installed data');
      return withGenerationLocks(names, action, index + 1);
    });
}
async function collectOldGenerations(dir, current, previous) {
  // Keep one prior generation for legacy tabs loaded before leases existed;
  // other leased generations are skipped until a later successful import.
  for (const name of await generations(dir)) {
    if (name === current || name === previous) continue;
    try {
      await navigator.locks.request(GENERATION_LOCK + name,
        {mode:'exclusive', ifAvailable:true}, async lock => {
          if (lock) await dir.removeEntry(name, {recursive:true});
        });
    } catch (_) {
      postMessage({type:'notice', message:'Could not clean up old installation ' + name +
        '; close other game tabs and retry on the next installation.'});
    }
  }
}

async function install(files) {
  if (active) throw new Error('An installation is already in progress');
  // Own the entire scan, validation and copy lifecycle, not only OPFS writes.
  active = true;
  cancelled = false;
  scanController = new AbortController();
  let dir = null;
  let generation = null;
  let committed = false;
  let previous = null;
  try {
    const {sources:selected, edition} = await selectSources(files);
    checkCancelled();
    // Validate before creating any replacement generation. Maps are MPQs too.
    postMessage({type:'phase', phase:'validating'});
    for (const file of selected) await checkMpqHeader(file);
    checkCancelled();
    const total = selected.reduce((sum, f) => sum + f.size, 0);
    if (!Number.isSafeInteger(total)) throw new Error('Installation size overflow');
    postMessage({type:'discovered', files:selected.map(f=>({name:f.name,size:f.size,source:f.source})), bytes:total});
    postMessage({type:'phase', phase:'storage'});
    // First recover safe space from obsolete generations. Without this step,
    // quota preflight could reject imports that would have fit after pruning.
    try { dir = await directory(false); }
    catch (err) { if (err.name !== 'NotFoundError') throw err; }
    if (dir) {
      try { previous = await metadata(dir); }
      catch (_) {
        // A corrupt manifest must not prevent the user repairing an install.
        postMessage({type:'notice', message:'Previous installation manifest unreadable; replacing it.'});
      }
      await collectOldGenerations(dir, previous && previous.generation, null);
    }
    await checkAvailableStorage(total);
    checkCancelled();
    if (!dir) dir = await directory(true);
    // Every import is a fresh immutable generation, committed by a manifest.
    generation = 'install-' + Date.now().toString(36) + '-' +
        Math.random().toString(36).slice(2, 10);
    const stage = await dir.getDirectoryHandle(generation, {create: true});
    let copied = 0;
    postMessage({type:'phase', phase:'copying'});
    for (const [index, f] of selected.entries()) {
      let fileCopied = 0;
      checkCancelled();
      const parts = f.name.split('/');
      let parent = stage;
      for (const part of parts.slice(0, -1))
        parent = await parent.getDirectoryHandle(part, {create:true});
      const dest = await parent.getFileHandle(parts[parts.length - 1], {create:true});
      const writable = await dest.createWritable();
      try {
        for (let pos = 0; pos < f.size; pos += CHUNK) {
          checkCancelled();
          const buffer = await f.blob.slice(pos, Math.min(f.size, pos + CHUNK)).arrayBuffer();
          checkCancelled();
          await writable.write(buffer);
          copied += buffer.byteLength;
          fileCopied += buffer.byteLength;
          postMessage({type:'progress', file:f.name, copied, total,
            index:index+1, count:selected.length, fileCopied, fileSize:f.size});
        }
        await writable.close();
      } catch (err) { await writable.abort().catch(()=>{}); throw err; }
    }
    checkCancelled();
    postMessage({type:'phase', phase:'committing'});
    const archiveNames = new Set(selected.map(f => f.name.toLowerCase()));
    const hasExpansion = archiveNames.has('war3x.mpq');
    const missing = hasExpansion && !archiveNames.has('war3xlocal.mpq') ?
      ['War3xLocal.mpq'] : [];
    const completeness = edition === 'demo' ? 'demo' : !hasExpansion ? 'base' :
      (missing.length ? 'partial-expansion' : 'expansion');
    const manifest = {
      version:3, generation, edition, completeness, missing,
      files:selected.map(f=>({name:f.name,size:f.size})), bytes:total
    };
    // This root manifest is the sole commit point. Do not remove any old
    // generation here: the engine may still have archives open from it.
    const handle = await dir.getFileHandle(META,{create:true});
    const out = await handle.createWritable();
    try {
      await out.write(JSON.stringify(manifest));
      if (cancelled) { await out.abort(); throw new Error('Import cancelled'); }
      await out.close();
    } catch (err) { await out.abort().catch(()=>{}); throw err; }
    committed = true;
    if (missing.length) postMessage({type:'notice', message:'Partial expansion installation: ' +
      missing.join(', ') + ' missing. Some campaign and localized content may not work.'});
    // A cleanup failure cannot undo a committed manifest. Previous readers
    // hold shared generation leases; skip them instead of deleting open files.
    try { await collectOldGenerations(dir, generation, previous && previous.generation); }
    catch (_) { postMessage({type:'notice', message:'Old installation cleanup could not finish; game files remain installed.'}); }
    postMessage({type:'done', manifest});
  } finally {
    if (dir && generation && !committed)
      await dir.removeEntry(generation,{recursive:true}).catch(()=>{});
    scanController = null;
    active = false;
  }
}
async function list() {
  try { postMessage({type:'list', manifest:await metadata(await directory(false))}); }
  catch (err) { if (err.name === 'NotFoundError') postMessage({type:'list',manifest:null}); else throw err; }
}
async function remove() {
  if (active) throw new Error('Cannot remove files while importing');
  const origin = await navigator.storage.getDirectory();
  try {
    const root = await origin.getDirectoryHandle(ROOT);
    const game = await root.getDirectoryHandle(GAME);
    const dir = await game.getDirectoryHandle(WC3);
    await withGenerationLocks([...(await generations(dir)), 'legacy'],
      () => game.removeEntry(WC3, {recursive:true}));
  } catch (err) { if (err.name !== 'NotFoundError') throw err; }
  postMessage({type:'list',manifest:null});
}
onmessage = async ({data}) => {
  if (data.type==='cancel') {
    if (active) { cancelled=true; if (scanController) scanController.abort(); }
    return;
  }
  try {
    if(data.type==='import') await withCommitLock(() => install(data.files));
    else if(data.type==='list') await list();
    else if(data.type==='remove') await withCommitLock(remove);
  } catch(err) {
    postMessage({type:'error', message:err.name==='AbortError' ? 'Import cancelled' : err.message||String(err)});
  }
};
