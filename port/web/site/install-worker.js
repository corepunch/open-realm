/* Game data never leaves the user's origin. This worker owns writes to OPFS. */
'use strict';
const ROOT = 'openrealm';
const GAME = 'game-data';
const WC3 = 'wc3';
const META = 'installation.json';
const CHUNK = 1024 * 1024;
const MAX_INPUT_FILES = 512; // Match filesystem-bridge.js manifest limit.
const MAX_ISO_IMAGES = 8; // Limit expensive full-directory scans per import.
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
    // The 2002 public demo has a legitimate War3.mpq header but is not the
    // retail game. Use distinctive *on-disc files*, never the user's ISO name.
    const discNames = new Set(entries.map(entry => entry.path.toLowerCase()));
    if (discNames.has('war3demo.ico') && discNames.has('scdemo.exe'))
      throw new Error('Warcraft III Demo disc detected in ' + file.name +
        '; the demo War3.mpq does not contain the complete retail game data');
    const onDisc = new Set();
    // Never apply RoC's anonymous FileNN map mapping to a TFT-only disc.
    const baseDisc = entries.some(entry => entry.name.toLowerCase() === 'war3.mpq');
    for (const entry of entries) {
      checkCancelled();
      const name = DISC_ARCHIVES[entry.name.toLowerCase()];
      if (!name) {
        if (entry.name.toLowerCase() === 'setup.mpq')
          setups.push({blob:file.slice(entry.offset, entry.offset + entry.size), source:file.name, roc:baseDisc});
        continue;
      }
      const key = name.toLowerCase();
      if (onDisc.has(key)) throw new Error('Ambiguous archive in ISO: ' + name);
      onDisc.add(key);
      // Validated extents are views into the original Blob, not ISO copies.
      add(name, file.slice(entry.offset, entry.offset + entry.size), file.name);
    }
  }
  // Never infer installer File000000NN names from an unknown release. An
  // observed retail RoC layout is supported only if its entire map set is
  // present; other Setup.mpq containers are skipped with a visible notice.
  if (setups.length) {
    postMessage({type:'phase', phase:'extracting'});
    if (!globalThis.SetupMPQ) importScripts('setup-mpq.js');
    for (const setup of setups) {
      checkCancelled();
      if (setup.roc === false) {
        // TFT discs commonly hold expansion data inside Setup.mpq. Recover
        // only members whose *internal* names identify whole game archives.
        const expansions = await SetupMPQ.extractNamedExpansionArchives(setup.blob,
          {signal:scanController.signal});
        for (const entry of expansions)
          add(entry.name, entry.blob, setup.source + ' / Setup.mpq');
        if (!expansions.length)
          postMessage({type:'notice', message:'No named expansion MPQs found inside ' +
            setup.source + ' / Setup.mpq; anonymous installer payloads need an edition-specific mapping.'});
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
  if (!names.has('war3.mpq')) {
    throw new Error('War3.mpq missing; select the Reign of Chaos base disc or MPQ alongside expansion data');
  }
  // An explicitly selected TFT Setup.mpq must not quietly produce a RoC-only
  // installation and report success. Retain the old generation instead.
  if (setups.some(setup => setup.roc === false) && !names.has('war3x.mpq')) {
    throw new Error('Could not recover War3x.mpq from the Frozen Throne Setup.mpq. ' +
      'This installer layout needs an identified member mapping; provide the ' +
      'installed War3x.mpq and War3xLocal.mpq alongside the Reign of Chaos data.');
  }
  return sources;
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
// Keep this probe aligned with common/mpq.c: it searches the first 1024
// bytes for MPQ\x1a. This is a fast format check, not full archive verification.
async function checkMpqHeader(file) {
  if (file.size < 32) throw new Error('Archive too small: ' + file.name);
  const probe = new Uint8Array(await file.blob.slice(0, 1024).arrayBuffer());
  checkCancelled();
  for (let i = 0; i + 32 <= probe.length; ++i) {
    if (probe[i] === 77 && probe[i+1] === 80 &&
        probe[i+2] === 81 && probe[i+3] === 26) return;
  }
  throw new Error('No supported MPQ header in ' + file.name);
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
  try {
    const selected = await selectSources(files);
    checkCancelled();
    // Validate before creating any replacement generation. Maps are MPQs too.
    postMessage({type:'phase', phase:'validating'});
    for (const file of selected) await checkMpqHeader(file);
    checkCancelled();
    const total = selected.reduce((sum, f) => sum + f.size, 0);
    if (!Number.isSafeInteger(total)) throw new Error('Installation size overflow');
    postMessage({type:'discovered', files:selected.map(f=>({name:f.name,size:f.size,source:f.source})), bytes:total});
    postMessage({type:'phase', phase:'storage'});
    await checkAvailableStorage(total);
    checkCancelled();
    dir = await directory(true);
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
    const manifest = {
      version:3, generation,
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
  const origin=await navigator.storage.getDirectory();
  try { const root=await origin.getDirectoryHandle(ROOT); const game=await root.getDirectoryHandle(GAME); await game.removeEntry(WC3,{recursive:true}); }
  catch(err) {if(err.name!=='NotFoundError') throw err;}
  postMessage({type:'list',manifest:null});
}
onmessage = async ({data}) => {
  if (data.type==='cancel') {
    if (active) { cancelled=true; if (scanController) scanController.abort(); }
    return;
  }
  try {
    if(data.type==='import') await install(data.files);
    else if(data.type==='list') await list();
    else if(data.type==='remove') await remove();
  } catch(err) {
    postMessage({type:'error', message:err.name==='AbortError' ? 'Import cancelled' : err.message||String(err)});
  }
};
