/* Game data never leaves the user's origin. This worker owns writes to OPFS. */
'use strict';
const ROOT = 'openrealm';
const GAME = 'game-data';
const WC3 = 'wc3';
const META = 'installation.json';
const CHUNK = 1024 * 1024;
// Only game archives are mountable. An installer container such as Setup.mpq
// must not be treated as game content: unpacking it is a separate feature.
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
  if (!Array.isArray(files) || !files.length) {
    throw new Error('Select MPQ, W3M, W3X or ISO files');
  }
  const images = files.filter(f => f && isoName(f.name));
  if (images.length) {
    // Patch 3 will implement multi-disc/mixed inputs and merge semantics.
    if (images.length !== 1 || files.length !== 1) {
      throw new Error('Select one ISO at a time; multi-disc imports are not yet supported');
    }
    const disc = images[0];
    if (!cleanName(disc.name) || typeof disc.slice !== 'function') {
      throw new Error('Invalid ISO filename');
    }
    // Load lazily: MPQ-only imports should work in existing deployments even
    // if an older site build does not yet ship the ISO reader.
    if (!globalThis.ISO9660) importScripts('iso9660.js');
    const image = await ISO9660.openISO(disc, {signal: scanController.signal});
    const entries = await image.listFiles();
    checkCancelled();
    const sources = [];
    const found = new Set();
    for (const entry of entries) {
      checkCancelled();
      const name = DISC_ARCHIVES[entry.name.toLowerCase()];
      if (!name) continue;
      const key = name.toLowerCase();
      if (found.has(key)) throw new Error('Ambiguous archive in ISO: ' + name);
      found.add(key);
      // ISO9660 validated this range. Blob.slice() creates a range view,
      // rather than allocating a second copy of the disc.
      sources.push({name, size: entry.size,
        blob: disc.slice(entry.offset, entry.offset + entry.size)});
    }
    if (!found.has('war3.mpq')) {
      throw new Error('ISO has no War3.mpq; the Reign of Chaos base disc is required');
    }
    return sources;
  }
  if (files.some(f => !f || !allowed(f.name) || !cleanName(f.name) ||
                       typeof f.slice !== 'function')) {
    throw new Error('Select only MPQ, W3M or W3X files with safe filenames');
  }
  return files.map(f => ({name:f.name, size:f.size, blob:f}));
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
    const names = new Set();
    for (const f of selected) {
      const key = f.name.toLowerCase();
      if (names.has(key)) throw new Error('Duplicate filename: ' + f.name);
      names.add(key);
    }
    // Validate before creating any replacement generation. Maps are MPQs too.
    for (const file of selected) await checkMpqHeader(file);
    checkCancelled();
    const total = selected.reduce((sum, f) => sum + f.size, 0);
    if (!Number.isSafeInteger(total)) throw new Error('Installation size overflow');
    dir = await directory(true);
    // Every import is a fresh immutable generation, committed by a manifest.
    generation = 'install-' + Date.now().toString(36) + '-' +
        Math.random().toString(36).slice(2, 10);
    const stage = await dir.getDirectoryHandle(generation, {create: true});
    let copied = 0;
    for (const f of selected) {
      checkCancelled();
      const dest = await stage.getFileHandle(f.name, {create:true});
      const writable = await dest.createWritable();
      try {
        for (let pos = 0; pos < f.size; pos += CHUNK) {
          checkCancelled();
          const buffer = await f.blob.slice(pos, Math.min(f.size, pos + CHUNK)).arrayBuffer();
          checkCancelled();
          await writable.write(buffer);
          copied += buffer.byteLength;
          postMessage({type:'progress', file:f.name, copied, total});
        }
        await writable.close();
      } catch (err) { await writable.abort().catch(()=>{}); throw err; }
    }
    checkCancelled();
    const manifest = {
      version:2, generation,
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
