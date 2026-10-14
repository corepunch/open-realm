/* Game data never leaves the user's origin. This worker owns writes to OPFS. */
'use strict';
const ROOT = 'openrealm';
const GAME = 'game-data';
const WC3 = 'wc3';
const META = 'installation.json';
const CHUNK = 1024 * 1024;
let cancelled = false;
let active = false;
const allowed = name => /\.(mpq|w3m|w3x)$/i.test(name);
const cleanName = name => typeof name === 'string' && /^[a-z0-9][a-z0-9 _().-]{0,200}$/i.test(name) && name !== '.' && name !== '..';
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
  const probe = new Uint8Array(await file.slice(0, 1024).arrayBuffer());
  for (let i = 0; i + 32 <= probe.length; ++i) {
    if (probe[i] === 77 && probe[i+1] === 80 &&
        probe[i+2] === 81 && probe[i+3] === 26) return;
  }
  throw new Error('No supported MPQ header in ' + file.name);
}
async function install(files) {
  if (active) throw new Error('An installation is already in progress');
  cancelled = false;
  const selected = files.filter(f => allowed(f.name) && cleanName(f.name));
  if (!selected.length || selected.length !== files.length) throw new Error('Select only MPQ, W3M or W3X files with safe filenames');
  const names = new Set();
  for (const f of selected) {
    const key = f.name.toLowerCase();
    if (names.has(key)) throw new Error('Duplicate filename: ' + f.name);
    names.add(key);
  }
  // Validate before creating any replacement generation. W3M/W3X maps are MPQs too.
  for (const file of selected) await checkMpqHeader(file);
  active = true;
  const dir = await directory(true);
  // Each import owns a new generation. Never modify the active generation.
  const generation = 'install-' + Date.now().toString(36) + '-' +
      Math.random().toString(36).slice(2, 10);
  const stage = await dir.getDirectoryHandle(generation, {create: true});
  let copied = 0;
  const total = selected.reduce((sum, f) => sum + f.size, 0);
  let committed = false;
  try {
    for (const f of selected) {
      if (cancelled) throw new Error('Import cancelled');
      const dest = await stage.getFileHandle(f.name, {create:true});
      const writable = await dest.createWritable();
      try {
        for (let pos = 0; pos < f.size; pos += CHUNK) {
          if (cancelled) throw new Error('Import cancelled');
          const buffer = await f.slice(pos, Math.min(f.size, pos + CHUNK)).arrayBuffer();
          await writable.write(buffer);
          copied += buffer.byteLength;
          postMessage({type:'progress', file:f.name, copied, total});
        }
        await writable.close();
      } catch (err) { await writable.abort().catch(()=>{}); throw err; }
    }
    if (cancelled) throw new Error('Import cancelled');
    // The root manifest is the only commit point. The old generation remains
    // readable unless the new generation is completely written and closed.
    const manifest = {
      version:2, generation,
      files:selected.map(f=>({name:f.name,size:f.size})), bytes:total
    };
    const handle = await dir.getFileHandle(META,{create:true});
    const out = await handle.createWritable();
    try {
      await out.write(JSON.stringify(manifest));
      if (cancelled) { await out.abort(); throw new Error('Import cancelled'); }
      await out.close();
    } catch (err) { await out.abort().catch(()=>{}); throw err; }
    committed = true;
    postMessage({type:'done', manifest});
    // Do not delete old generations here: a running engine may still be
    // reading one. Garbage collection requires an explicit idle phase.
  } finally {
    if (!committed) await dir.removeEntry(generation,{recursive:true}).catch(()=>{});
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
  if (data.type==='cancel') {if (active) cancelled=true;return;}
  try {
    if(data.type==='import') await install(data.files);
    else if(data.type==='list') await list();
    else if(data.type==='remove') await remove();
  } catch(err) { active=false; postMessage({type:'error', message:err.message||String(err)}); }
};
