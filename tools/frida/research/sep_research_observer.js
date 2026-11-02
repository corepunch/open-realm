// SEP-01.2..SEP-04.3 research observer for WC3 1.27.1.7085 (game.dll sha256 d51e5680...d8236).
// Read-only: Interceptor entry/leave + two instruction probes inside Separate_Update. No game calls, no writes.
// `config` is prepended by tools/frida/research/sep_research_trace.py.
let installed = false, recording = true;
const counts = {};
const emit = (event, data = {}) => { if (recording) send({event, ...data}); };
const bump = k => { counts[k] = (counts[k] || 0) + 1; return counts[k]; };
const u32s = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32());

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString()});
    const R = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(R(rva), cb);
    const owner = () => R(0xd53a48).readPointer();
    const ownerWords = () => { const o = owner(); return o.isNull() ? null : u32s(o, 2); };
    const visit = () => { const o = owner(); return o.isNull() ? null : o.add(0x538).readU32(); };
    const caller = ctx => ctx.returnAddress.sub(base).toUInt32();
    let tick = -1;            // last JASS tick marker seen
    const unitOfMover = new Map();
    let refreshUnit = null;   // Unit_RefreshSeparationConfiguration in progress (single game thread)

    const moverState = m => {
        const occ = m.add(0x98).readPointer(), prox = m.add(0x94).readPointer();
        return {pos: u32s(m.add(0x78), 2), vel: u32s(m.add(0x80), 2), speed: m.add(0xc0).readU32(),
            radius: m.add(0x90).readU32(), mask: m.add(0xa8).readU32(), flags14: m.add(0x14).readU32(),
            occ: occ.isNull() ? null : u32s(occ.add(0x1c), 4), occFlags: occ.isNull() ? null : occ.add(0x40).readU32(),
            prox: prox.isNull() ? null : u32s(prox.add(0x1c), 4), proxMap: prox.isNull() ? null : prox.add(0x2c).readPointer().toString()};
    };
    const sepState = s => ({vec: u32s(s.add(0x18), 2), word: s.add(0x20).readU32()});

    // --- Markers (Preload string intern; same site as the core observer) ---
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value || !value.startsWith('PATHSEP')) return;
        const m = /tick=(\d+)/.exec(value); if (m) tick = +m[1];
        emit('marker', {value, owner: ownerWords(), visit: visit()});
    }});

    // --- Policy producers ---
    hook(0x693d50, {  // Unit_RefreshSeparationConfiguration(ECX unit), plain RET
        onEnter() { const u = this.context.ecx; refreshUnit = u;
            this.row = {unit: u.toString(), caller: caller(this), tick, rawcode: u.add(0x30).readU32(), ownerIndex: u.add(0x58).readU32(),
                f20: u.add(0x20).readU32(), f54: u.add(0x54).readS32(), f5c: u.add(0x5c).readU32(), f60: u.add(0x60).readU32(),
                f198: u.add(0x198).readS32()}; },
        onLeave() { refreshUnit = null; bump('refresh');
            emit('refresh', {...this.row, mover: this.mover ? this.mover.toString() : null, sep: this.sep || null}); }
    });
    hook(0x1710e0, {  // Mover_ConfigureSeparation(ECX mover, enable, selector, category, rank), RET 0x10
        onEnter(args) { this.m = this.context.ecx;
            this.row = {mover: this.m.toString(), caller: caller(this), tick, args: [0, 1, 2, 3].map(i => args[i].toUInt32()),
                unit: refreshUnit ? refreshUnit.toString() : null, oldSep: this.m.add(0xac).readPointer().toString()};
            if (refreshUnit) unitOfMover.set(this.m.toString(), refreshUnit.toString()); },
        onLeave() { const s = this.m.add(0xac).readPointer(); bump('configure');
            emit('configure', {...this.row, sep: s.toString(), state: s.isNull() ? null : sepState(s)}); }
    });

    // --- Separate_Update (ECX separation, [esp+4] scratch query), RET 4 ---
    const visits = new Map();  // threadless: one game thread
    let current = null;
    hook(0x1702f0, {
        onEnter() { const s = this.context.ecx, m = s.add(0x14).readPointer();
            current = this.cur = {sep: s.toString(), mover: m.toString(), tick, visit: visit(), before: sepState(s),
                moverBefore: moverState(m), ownerBefore: ownerWords(), apply: null, pairs: [], draws: [], query: null};
            this.s = s; this.m = m; },
        onLeave() { const c = this.cur; current = null; bump('sep-update');
            emit('sep-update', {...c, after: sepState(this.s), moverAfter: moverState(this.m), ownerAfter: ownerWords()}); }
    });
    hook(0x16ffa0, {  // Separate_ApplyRetained (ECX separation)
        onEnter() { this.s = this.context.ecx; this.m = this.s.add(0x14).readPointer();
            this.row = {vec: u32s(this.s.add(0x18), 2), pos: u32s(this.m.add(0x78), 2), occ: moverState(this.m).occ, valid: null}; if (current) current.apply = this.row; },
        onLeave() { this.row.posAfter = u32s(this.m.add(0x78), 2); this.row.occAfter = moverState(this.m).occ; }
    });
    hook(0x16ee80, {  // endpoint validation (ECX separation, [esp+4] point)
        onEnter(args) { this.p = u32s(args[0], 2); },
        onLeave(r) { if (current && current.apply && caller(this) === 0x17002e) { current.apply.endpoint = this.p; current.apply.valid = r.toUInt32(); } }
    });
    hook(0x16f570, {  // candidate collection (ECX query)
        onEnter() { this.q = this.context.ecx; },
        onLeave() { const q = this.q, n = q.add(0x1c).readU32(), d = q.add(0xc).readPointer(), members = [];
            if (n > 4096) throw new Error('candidate count');
            for (let i = 0; i < n; i++) { const obj = d.add(i * 8).readPointer(), mv = obj.add(0x30).readPointer();
                const sp = mv.add(0xac).readPointer();
                members.push({obj: obj.toString(), mover: mv.toString(), word: sp.isNull() ? null : sp.add(0x20).readU32()}); }
            const row = {source: q.add(0x40).readPointer().toString(), rect: u32s(q.add(0x24), 4), point: u32s(q.add(0x44), 2),
                radius: q.add(0x4c).readU32(), category: q.add(0x50).readU16(), rank: q.add(0x52).readU8(), members};
            if (current) current.query = row; else emit('query-outside', row); }
    });
    hook(0x16e830, {  // Separate_FilterCandidate(ECX spatial object, EDX query), plain RET
        onEnter() { const o = this.context.ecx, q = this.context.edx; this.row = null;
            if (!current) return;
            const mv = o.add(0x30).readPointer();
            const r = {obj: o.toString(), obj34: o.add(0x34).readU32(), mover: mv.toString()};
            if (!mv.isNull()) { r.tag = mv.add(0x10).readU32(); r.flags14 = mv.add(0x14).readU32(); r.radius = mv.add(0x90).readU32();
                r.speed = mv.add(0xc0).readU32(); const sp = mv.add(0xac).readPointer(); r.word = sp.isNull() ? null : sp.add(0x20).readU32(); }
            r.qcat = q.add(0x50).readU16(); r.qrank = q.add(0x52).readU8(); this.row = r; },
        onLeave(ret) { if (this.row && current) { this.row.result = ret.toUInt32(); (current.filter = current.filter || []).push(this.row); } }
    });
    if (config.pairProbes) {
        // 6f1703e0: both positions resolved; EDI = candidate index; EBX = separation; [ebp-0x68..-0x5c] source/candidate.
        Interceptor.attach(R(0x1703e0), function () { if (!current) return; const bp = this.context.ebp;
            current.pairs.push({index: this.context.edi.toUInt32(), source: u32s(bp.sub(0x68), 2), candidate: u32s(bp.sub(0x60), 2),
                vecBefore: u32s(this.context.ebx.add(0x18), 2), ownerBefore: ownerWords()}); });
        // 6f170518: after (optional) accumulation for this candidate.
        Interceptor.attach(R(0x170518), function () { if (!current || !current.pairs.length) return; const p = current.pairs[current.pairs.length - 1];
            p.vecAfter = u32s(this.context.ebx.add(0x18), 2); p.distance = this.context.ebp.sub(4).readU32(); p.ownerAfter = ownerWords(); });
    }
    hook(0x1d19e0, {  // overlap direction (ECX out, EDX PRNG state)
        onEnter() { this.out = this.context.ecx; this.st = this.context.edx; this.before = u32s(this.st, 2); },
        onLeave() { const row = {state: this.st.toString(), before: this.before, after: u32s(this.st, 2), direction: u32s(this.out, 2), caller: caller(this)};
            bump('overlap-draw'); if (current) current.draws.push(row); else emit('overlap-draw', {...row, tick}); }
    });
    // --- shared path-owner PRNG stream (ECX state); only the path owner instance is recorded ---
    hook(0x1b7130, {
        onEnter() { this.observe = this.context.ecx.equals(owner()); if (this.observe) this.before = u32s(this.context.ecx, 2);
            this.c = caller(this); },
        onLeave(r) { if (!this.observe) return; bump('owner-draw');
            emit('owner-draw', {tick, caller: this.c, before: this.before, after: ownerWords(), value: r.toUInt32(), visit: visit(), inSep: current ? current.sep : null}); }
    });
    if (config.retryEvents) {
        // 6fd53a8c: mover whose path request is being processed (read the same way by the core observer's retry rows).
        const cur = () => R(0xd53a8c).readPointer().toString();
        hook(0x1689d0, {onEnter() { this.p = this.context.ecx; this.b = ownerWords(); this.cur = cur(); },
            onLeave() { bump('retry-init'); emit('retry-init', {tick, path: this.p.toString(), current: this.cur, count: this.p.add(0x98).readU32(), ownerBefore: this.b, ownerAfter: ownerWords(), visit: visit()}); }});
        hook(0x167290, {onEnter() { this.p = this.context.ecx; this.before = this.p.add(0x98).readU32(); this.b = ownerWords(); this.cur = cur(); },
            onLeave(r) { bump('retry-result'); emit('retry-result', {tick, path: this.p.toString(), current: this.cur, before: this.before, after: this.p.add(0x98).readU32(),
                result: r.toInt32(), ownerBefore: this.b, ownerAfter: ownerWords(), visit: visit()}); }});
        hook(0x171340, {onEnter() { this.m = this.context.ecx; bump('mover-stop');
            emit('mover-stop', {tick, mover: this.m.toString(), caller: caller(this), speed: this.m.add(0xc0).readU32(), pos: u32s(this.m.add(0x78), 2)}); }});
    }
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() { recording = false; return {installed, counts}; }};
