// NUM-04.5/NUM-04.6 research observer for WC3 1.27.1.7085 (game.dll sha256 d51e5680...d8236).
// Read-only: Interceptor entry/leave and instruction probes. No game calls, no memory writes.
// Records every PathRandom_Seed (1cc4e0) and PathRandom_Next (1b7130) call from module load, classifying
// the state pointer as path owner, one of the 45 TLS unit streams (693710 table), a static game.dll global,
// or another (stack/heap) state, plus the setup seed decision and lifecycle markers.
// `config` is prepended by tools/frida/research/NUM-04.5_trace.py.
let installed = false, recording = true, seq = 0;
const counts = {}, agg = {}, buffer = [];
const flush = () => { if (buffer.length) { send({event: 'batch', rows: buffer.splice(0)}); } };
const emit = (event, data = {}) => { if (!recording) return; buffer.push({event, seq: ++seq, ms: Date.now(), ...data}); if (buffer.length >= 256) flush(); };
const bump = k => { counts[k] = (counts[k] || 0) + 1; return counts[k]; };
const u32s = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32());
const DETAIL_CAP = config.detailCap || 150000;

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    const imageEnd = base.add(config.imageSize);
    emit('module', {base: base.toString(), thread: Process.getCurrentThreadId()});
    const R = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(R(rva), cb);
    const owner = () => R(0xd53a48).readPointer();
    const ownerWords = () => { const o = owner(); return o.isNull() ? null : u32s(o, 2); };
    const visit = () => { const o = owner(); return o.isNull() ? null : o.add(0x538).readU32(); };
    const caller = ctx => { const r = ctx.returnAddress; return r.compare(base) >= 0 && r.compare(imageEnd) < 0 ? r.sub(base).add(0x6f000000).toUInt32() : 'ext:' + r.toString(); };
    const netFlags = () => { const n = R(0xd687a8).readPointer(); if (n.isNull()) return null; const s = n.add(0x30).readPointer(); return s.isNull() ? null : s.add(0x38).readU32(); };
    let table = null;     // 693710 table object (TLS slot 0xd registry entry 3); streams at +4 + 8*i
    let tableThread = null;
    let firstSep = false, firstMove = false, firstOwnerVisit = false, phase = 'boot';
    const classify = p => {
        const o = owner();
        if (!o.isNull() && p.equals(o)) return {kind: 'owner'};
        if (table !== null) {
            const d = p.sub(table.add(4));
            const off = d.toInt32();
            if (p.compare(table.add(4)) >= 0 && off < 45 * 8 && (off & 7) === 0) return {kind: 'stream', index: off >> 3};
        }
        if (p.compare(base) >= 0 && p.compare(imageEnd) < 0) return {kind: 'global', va: p.sub(base).add(0x6f000000).toUInt32()};
        return {kind: 'other'};
    };
    const label = c => c.kind === 'stream' ? 'stream:' + c.index : c.kind === 'global' ? 'global:' + c.va.toString(16) : c.kind;
    const snapStreams = () => table === null ? null : Array.from({length: 45}, (_, i) => u32s(table.add(4 + 8 * i), 2));

    // --- generator primitives ---
    hook(0x1cc4e0, {  // PathRandom_Seed(ECX state, [esp+4] seed), RET 4
        onEnter(args) {
            this.p = this.context.ecx; this.seed = args[0].toUInt32(); this.c = caller(this); this.cls = classify(this.p);
        },
        onLeave() {
            const k = bump('seed:' + label(this.cls));
            const sk = 'seed:' + (this.cls.kind === 'stream' ? 'stream' : label(this.cls)) + '@' + (typeof this.c === 'number' ? this.c.toString(16) : this.c) + '#' + phase;
            agg[sk] = (agg[sk] || 0) + 1;
            if (k <= 64 || this.cls.kind === 'owner' || this.cls.kind === 'stream')
                emit('seed', {state: this.p.toString(), cls: label(this.cls), seed: this.seed, caller: this.c, after: u32s(this.p, 2),
                    thread: Process.getCurrentThreadId(), phase, visit: visit()});
        }
    });
    hook(0x1b7130, {  // PathRandom_Next(ECX state) -> EAX
        onEnter() {
            this.p = this.context.ecx; this.cls = classify(this.p);
            this.c = caller(this);
            if (this.cls.kind === 'owner' || this.cls.kind === 'stream') this.before = u32s(this.p, 2);
        },
        onLeave(r) {
            const key = label(this.cls) + '@' + (typeof this.c === 'number' ? this.c.toString(16) : this.c) + '#' + phase;
            agg[key] = (agg[key] || 0) + 1;
            if ((this.cls.kind === 'owner' || this.cls.kind === 'stream') && bump('detail') <= DETAIL_CAP)
                emit('draw', {cls: label(this.cls), caller: this.c, before: this.before, after: u32s(this.p, 2), value: r.toUInt32(),
                    thread: Process.getCurrentThreadId(), phase, visit: visit()});
        }
    });
    // --- unit-stream table ---
    hook(0x693710, {  // Streams_Reseed45(ECX seed), plain RET
        onEnter() {
            this.seed = this.context.ecx.toUInt32(); this.c = caller(this);
            this.thread = Process.getCurrentThreadId();
        },
        onLeave() {
            emit('streams-reseed', {seed: this.seed, caller: this.c, table: table ? table.toString() : null,
                vtable: table ? table.readPointer().sub(base).add(0x6f000000).toUInt32() : null, word0: table ? table.readU32() : null,
                streams: snapStreams(), thread: this.thread, owner: ownerWords(), phase, visit: visit()});
        }
    });
    // The table pointer is loaded at 6f69372c (esi = [eax+0xc]); probe the instruction after it.
    Interceptor.attach(R(0x693734), function () {
        const t = this.context.esi; if (table === null || !table.equals(t)) { table = t; tableThread = Process.getCurrentThreadId();
            emit('streams-table', {table: t.toString(), vtable: t.readPointer().sub(base).add(0x6f000000).toUInt32(), thread: tableThread}); }
    });
    for (const [rva, idxReg, name] of [[0x693660, 'ecx', 'stream-int'], [0x6936a0, 'edx', 'stream-real'], [0x695c70, 'edx', 'stream-range']]) {
        hook(rva, {onEnter() { const i = this.context[idxReg].toUInt32(); const c = caller(this);
            const key = name + ':' + i + '@' + (typeof c === 'number' ? c.toString(16) : c) + '#' + phase; agg[key] = (agg[key] || 0) + 1; }});
    }
    // --- setup seed producer (29e300) ---
    hook(0x29e300, {
        onEnter() { this.mode = this.context.ecx.toUInt32(); this.c = caller(this); phase = 'setup';
            emit('setup-enter', {mode: this.mode, caller: this.c, flags: netFlags(), owner: ownerWords()}); },
        onLeave() { emit('setup-leave', {mode: this.mode, flags: netFlags(), owner: ownerWords()}); phase = 'post-setup'; }
    });
    Interceptor.attach(R(0x29e5c4), function () {  // per-slot contribution before fold
        const bp = this.context.ebp;
        emit('setup-slot', {slot: bp.sub(0x3c1d).readU8(), value: bp.sub(0x3ca8).readU32(), second: bp.sub(0x3ca4).readU32(), seedBefore: bp.sub(0x3c50).readU32()});
    });
    Interceptor.attach(R(0x29e6ee), function () {  // seed passed to the local slot-shuffle PRNG
        emit('setup-shuffle-seed', {seed: this.context.ebp.sub(0x3c50).readU32()});
    });
    Interceptor.attach(R(0x29ec0d), function () {  // test [eax+0x38],0x8000: lock flag decision
        emit('setup-seed-decision', {flags: this.context.eax.add(0x38).readU32(), lobbySeed: this.context.ebp.sub(0x3c50).readU32(), owner: ownerWords()});
    });
    hook(0x2a0c80, {  // setup descriptor -> NetState (flag OR at 2a0d0e)
        onEnter() { const d = this.context.ecx; this.d = d;
            emit('setup-descriptor', {caller: caller(this), d30: d.add(0x30).readU8(), d31: d.add(0x31).readU8(), d34: d.add(0x34).readU32(), d3c: d.add(0x3c).readU32(),
                d9c: d.add(0x9c).readU32(), da0: d.add(0xa0).readU32(), flags: netFlags()}); },
        onLeave(r) { emit('setup-descriptor-leave', {result: r.toUInt32(), flags: netFlags()}); }
    });
    hook(0x213b50, {onEnter(args) { emit('jass-setmapflag', {flag: args[0].toUInt32(), value: args[1].toUInt32(), before: netFlags()}); }});
    hook(0x1e9dd0, {
        onEnter() { this.b = ownerWords(); this.c = caller(this); },
        onLeave() { emit('resolve-races', {caller: this.c, before: this.b, after: ownerWords(), flags: netFlags()}); }
    });
    hook(0x214140, {onEnter(args) { emit('jass-setrandomseed', {seed: args[0].toUInt32(), owner: ownerWords(), phase}); }});
    // --- script/setup markers (public JASS natives, entry only) ---
    hook(0x213bb0, {onEnter() { emit('jass-config-setmapname', {owner: ownerWords(), flags: netFlags(), phase}); }});
    hook(0x211d20, {onEnter() { emit('jass-main-setcamerabounds', {owner: ownerWords(), flags: netFlags(), phase}); }});
    hook(0x213f30, {onEnter(args) { emit('jass-setplayerracepreference', {pref: args[1].toUInt32(), owner: ownerWords(), phase}); }});
    Interceptor.attach(R(0x2a46ec), function () {  // GetTickCount() result stored into the local setup record seed
        emit('setup-record-tick', {tick: this.context.eax.toUInt32(), record: this.context.edi.add(0x18).toString()});
    });
    hook(0x71d580, {  // WorldEdit preference bool (index 0x6a = 'Test Map - Fixed Random Seed', default 1)
        onEnter() { this.i = this.context.ecx.toUInt32(); this.def = this.context.edx.toUInt32(); this.c = caller(this); },
        onLeave(r) { emit('pref-bool', {index: this.i, useDefault: this.def, result: r.toUInt32(), caller: this.c}); }
    });
    hook(0x29dfb0, {onEnter() { emit('replay-setup-enter', {caller: caller(this), owner: ownerWords()}); }});
    // --- lifecycle markers ---
    hook(0x15aa80, {onEnter() { if (!firstOwnerVisit) { firstOwnerVisit = true; phase = 'running';
        emit('first-owner-visit', {owner: ownerWords(), visit: visit(), streams: snapStreams()}); } }});
    hook(0x170880, {onEnter() { if (!firstMove) { firstMove = true;
        emit('first-mover-update', {mover: this.context.ecx.toString(), owner: ownerWords(), visit: visit(), streams: snapStreams()}); } }});
    hook(0x1702f0, {onEnter() { if (bump('sep') <= (config.sepSamples || 3))
        emit('separation-visit', {sep: this.context.ecx.toString(), owner: ownerWords(), visit: visit(), streams: counts['sep'] === 1 ? snapStreams() : null}); }});
    hook(0x231df0, {onEnter(args) {  // Preload string intern (marker site used by prior observers)
        if (args[0].isNull()) return;
        let v; try { v = args[0].readCString(); } catch (e) { return; }
        if (!v || !(config.markerPrefixes || []).some(p => v.startsWith(p))) return;
        emit('marker', {value: v, owner: ownerWords(), visit: visit()});
    }});
    flush();
}

Process.attachModuleObserver({onAdded: install});
setInterval(flush, 200);
rpc.exports = {finish() {
    const result = {installed, counts, agg};
    recording = false; flush();
    return result;
}};
