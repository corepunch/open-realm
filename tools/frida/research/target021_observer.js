// TARGET-02.1 / TARGET-03.x read-only observer for WC3 1.27.1.7085 game.dll. No calls into game code,
// no writes. Markers: Preload string intern 231df0 (config.prefix). While a scene window is open
// (begin-setup .. end-cleanup markers, or always when config.always) it records, per owner counter
// (owner d53a48 +538):
//   gtick/gtick-end  16c150 every group owner visit: target identity +40, flags +80, refresh countdown +64,
//                    unseen +6c, path +3c destination/timestamps/route indices and every member mover/path;
//   sample           16cd30 cached-destination sampler (out vector, countdown/unseen before/after);
//   vis              23a760 group visibility callback (EDX group, ECX target mover, EAX blocked);
//   req              16ce10 group route request (ready/final outputs, path after);
//   gate             167e40 destination-change gate (caller, old/new words, timestamps, changed/ready);
//   interval         168910 request interval (mode, current, timestamp before/after, elapsed, result);
//   admit            168310 scheduler admission (bucket words, result);
//   coarse/fine      166c30/166e90 route requests with pops (166db6 eax / 166fd0 eax) and bucket work;
//   setdest          168b80 destination writes; activate 166060; refresh 169680 reloads;
//   begin-target     05a5c0 target request setup; begin-task 5fc640 Move target task;
//   validate         5fb940 CAbilityMove target validation (result 0/a9/aa/dd plus target +20/+5c words);
//   target-lost      651010 CEventTargetLost producer caller; on-target-lost 5ff490 handler entry/exit.
let installed = false, recording = true, active = !!config.always;
const counts = {};
// Rows are batched (256 per message, flushed at every marker and at finish) to avoid per-message backpressure;
// the controller expands each batch back into one JSONL row per event, preserving order.
const batch = [];
const flush = () => {if (batch.length) send({event: 'batch', rows: batch.splice(0, batch.length)});};
const emit = (event, data = {}) => {if (!recording) return; batch.push({event, ms: Date.now(), ...data}); if (batch.length >= 256) flush();};
const bump = k => {counts[k] = (counts[k] || 0) + 1;};
const u32 = p => p.readU32() >>> 0;
const LIMIT = config.limit || 400000;
// config.lite (crowd scenes): compact group snapshots and only decision-changing gate/req/sample/vis rows
// (gate changed or not ready, req failed/not ready/final, sample at countdown 0 or unseen change, vis blocked);
// suppressed rows are not emitted and not counted.
const LITE = !!config.lite;

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);
    const counter = () => {const o = at(0xd53a48).readPointer(); return o.isNull() ? null : u32(o.add(0x538));};
    const caller = ctx => ctx.returnAddress.sub(base).toUInt32();
    const words = (p, n) => {const r = []; for (let i = 0; i < n; i++) r.push(u32(p.add(4 * i))); return r;};
    const out = (event, row) => {
        bump(event);
        if (!active) return;
        if ((counts[event + '-emitted'] || 0) >= LIMIT) {bump(event + '-dropped'); return;}
        bump(event + '-emitted');
        emit(event, row);
    };
    const pathSnap = p => {
        if (p.isNull()) return null;
        return {p: p.toString(), id: words(p.add(0x14), 2), dest: words(p.add(0x1c), 2), adj: words(p.add(0x24), 2),
            orig: words(p.add(0x2c), 2), times: words(p.add(0x7c), 2), idx: [p.add(0x74).readS32(), p.add(0x78).readS32()],
            cnt: [u32(p.add(0x50)), u32(p.add(0x70))], flags: u32(p.add(0x88)), retry: words(p.add(0x94), 2),
            links: words(p.add(0x8c), 2), limits: [p.add(0x84).readU16(), p.add(0x86).readU16()], fp: u32(p.add(0xb4)),
            ft: p.add(0xa4).readPointer().toString()};
    };
    const moverSnap = m => ({m: m.toString(), id: words(m.add(0x14), 2), pos: words(m.add(0x78), 2), vel: words(m.add(0x80), 2),
        r: u32(m.add(0x90)), range: u32(m.add(0xb0)), flags: u32(m.add(0xd8)), grp: words(m.add(0x9c), 2), path: pathSnap(m.add(0xa8).readPointer())});
    const litePath = p => p.isNull() ? null : {p: p.toString(), dest: words(p.add(0x1c), 2), times: words(p.add(0x7c), 2),
        idx: [p.add(0x74).readS32(), p.add(0x78).readS32()], cnt: [u32(p.add(0x50)), u32(p.add(0x70))]};
    const liteGroup = g => {
        const count = u32(g.add(0x38)), data = g.add(0x28).readPointer(), members = [];
        for (let i = 0; i < Math.min(count, 16); i++) {
            const m = data.add(i * 0x2c + 0x14).readPointer();
            if (!m.isNull()) members.push({id: words(m.add(0x14), 2), pos: words(m.add(0x78), 2), path: litePath(m.add(0xa8).readPointer())});
        }
        return {g: g.toString(), id: words(g.add(0x14), 2), target: words(g.add(0x40), 2), flags: u32(g.add(0x80)), cd: g.add(0x64).readS32(),
            unseen: u32(g.add(0x6c)), count, path: litePath(g.add(0x3c).readPointer()), members, lite: 1};
    };
    const groupSnap = g => {
        if (LITE) return liteGroup(g);
        const count = u32(g.add(0x38)), data = g.add(0x28).readPointer(), members = [];
        for (let i = 0; i < Math.min(count, 16); i++) {
            const e = data.add(i * 0x2c), m = e.add(0x14).readPointer();
            members.push({ef: u32(e.add(0x28)), ...(m.isNull() ? {m: null} : moverSnap(m))});
        }
        return {g: g.toString(), id: words(g.add(0x14), 2), target: words(g.add(0x40), 2), flags: u32(g.add(0x80)),
            cd: g.add(0x64).readS32(), unseen: u32(g.add(0x6c)), age: u32(g.add(0x5c)), comp: u32(g.add(0x60)),
            cool: u32(g.add(0x68)), off: words(g.add(0x4c), 2), form: words(g.add(0x54), 2), count,
            path: pathSnap(g.add(0x3c).readPointer()), members};
    };
    const safe = (f, fallback) => {try {return f();} catch (e) {return {error: String(e), ...(fallback || {})};}};

    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value || !value.startsWith(config.prefix)) return;
        const label = (value.match(/ label=([a-z0-9-]+)/) || [])[1] || '';
        if (label === 'begin-setup') active = true;
        emit('marker', {value, c: counter()});
        bump('marker');
        flush();
        if (label === 'end-cleanup' && !config.always) active = false;
    }});
    hook(0x16c150, {
        onEnter() {this.g = this.context.ecx; if (active) out('gtick', {c: counter(), ...safe(() => groupSnap(this.g))});},
        onLeave() {if (active) out('gtick-end', {c: counter(), g: this.g.toString(), ...safe(() => ({cd: this.g.add(0x64).readS32(),
            unseen: u32(this.g.add(0x6c)), flags: u32(this.g.add(0x80)), path: LITE ? null : pathSnap(this.g.add(0x3c).readPointer())}))});}
    });
    hook(0x16cd30, {
        onEnter(args) {this.g = this.context.ecx; this.o = args[0]; this.b = active ? {cd: this.g.add(0x64).readS32(), unseen: u32(this.g.add(0x6c))} : null;},
        onLeave() {if (this.b && (!LITE || this.b.cd === 0 || this.b.unseen !== u32(this.g.add(0x6c)))) out('sample', {c: counter(), g: this.g.toString(), before: this.b, cd: this.g.add(0x64).readS32(),
            unseen: u32(this.g.add(0x6c)), flags: u32(this.g.add(0x80)), dest: words(this.o, 2)});}
    });
    hook(0x23a760, {
        onEnter() {this.g = this.context.edx; this.t = this.context.ecx;},
        onLeave(ret) {if (active && (!LITE || (ret.toUInt32() & 0xff))) out('vis', {c: counter(), g: this.g.toString(), t: this.t.toString(), blocked: ret.toUInt32() & 0xff});}
    });
    hook(0x16ce10, {
        onEnter(args) {this.g = this.context.ecx; this.d = args[0]; this.r = args[2]; this.f = args[3]; this.in = active ? words(args[0], 2) : null;},
        onLeave(ret) {if (this.in && (!LITE || !ret.toUInt32() || !u32(this.r) || u32(this.f))) out('req', {c: counter(), g: this.g.toString(), in: this.in, result: ret.toUInt32(),
            ready: u32(this.r), final: u32(this.f), path: pathSnap(this.g.add(0x3c).readPointer())});}
    });
    hook(0x167e40, {
        onEnter(args) {this.row = active ? {c: counter(), caller: caller(this), p: this.context.ecx.toString(), old: words(this.context.ecx.add(0x1c), 2),
            nw: words(args[0], 2), shift: args[1].toUInt32(), times: words(this.context.ecx.add(0x7c), 2)} : null; this.r = args[2];},
        onLeave(ret) {if (this.row && (!LITE || ret.toUInt32() || !u32(this.r))) out('gate', {...this.row, changed: ret.toUInt32(), ready: u32(this.r)});}
    });
    hook(0x168910, {
        onEnter(args) {this.p = this.context.ecx; this.mode = args[0].toUInt32(); this.e = args[2];
            this.row = active ? {c: counter(), caller: caller(this), p: this.p.toString(), mode: this.mode, cur: args[1].toUInt32(),
                before: u32(this.p.add(0x7c + 4 * this.mode))} : null;},
        onLeave(ret) {if (this.row) out('interval', {...this.row, after: u32(this.p.add(0x7c + 4 * this.mode)), elapsed: u32(this.e), result: ret.toUInt32()});}
    });
    hook(0x168310, {
        onEnter(args) {this.b = this.context.ecx; this.row = active ? {c: counter(), caller: caller(this), bucket: this.b.toString(), p: args[0].toString(),
            limit: u32(this.b.add(4)), work: u32(this.b.add(8)), countdown: u32(this.b.add(0xc)), qcount: u32(this.b.add(0x10)),
            head: this.b.add(0x14).readPointer().toString(), queued: u32(args[0].add(0x8c))} : null;},
        onLeave(ret) {if (this.row) out('admit', {...this.row, result: ret.toUInt32(), qcountAfter: u32(this.b.add(0x10))});}
    });
    const pops = new Map();
    hook(0x166db6, {onEnter() {if (active) pops.set(this.context.ebx.toString(), {pops: this.context.eax.toUInt32(), work: u32(this.context.esi.add(8))});}});
    hook(0x166fd0, {onEnter() {if (active) pops.set(this.context.ebx.toString(), {pops: this.context.eax.toUInt32(), work: u32(this.context.ecx.add(8))});}});
    for (const [rva, name] of [[0x166c30, 'coarse'], [0x166e90, 'fine']]) hook(rva, {
        onEnter() {this.p = this.context.ecx; this.row = active ? {c: counter(), caller: caller(this), p: this.p.toString(), times: words(this.p.add(0x7c), 2)} : null;
            if (this.row) pops.delete(this.p.toString());},
        onLeave(ret) {if (!this.row) return; const k = this.p.toString(), s = pops.get(k); pops.delete(k);
            out(name, {...this.row, result: ret.toUInt32(), timesAfter: words(this.p.add(0x7c), 2), search: s || null,
                cnt: [u32(this.p.add(0x50)), u32(this.p.add(0x70))], idx: [this.p.add(0x74).readS32(), this.p.add(0x78).readS32()]});}
    });
    hook(0x168b80, {
        onEnter(args) {this.p = this.context.ecx; this.row = active ? {c: counter(), caller: caller(this), p: this.p.toString(), dest: words(args[0], 2),
            replace: args[1].toUInt32(), times: words(this.p.add(0x7c), 2)} : null;},
        onLeave() {if (this.row) out('setdest', this.row);}
    });
    hook(0x166060, {onEnter(args) {if (active) out('activate', {c: counter(), caller: caller(this), p: this.context.ecx.toString(), times: words(this.context.ecx.add(0x7c), 2)});}});
    const refresh = new Map();
    hook(0x169680, {
        onEnter() {this.g = this.context.ecx; this.reload = active && this.g.add(0x64).readS32() === -1; if (this.reload) refresh.set(this.g.toString(), {});},
        onLeave() {if (!this.reload) return; const k = this.g.toString(), r = refresh.get(k) || {}; refresh.delete(k);
            out('refresh', {c: counter(), g: k, reload: this.g.add(0x64).readS32(), flags: u32(this.g.add(0x80)), ...r});}
    });
    hook(0x169727, {onEnter() {const r = refresh.get(this.context.ebx.toString()); if (r) r.dist = u32(this.context.ebp.sub(8));}});
    hook(0x16974d, {onEnter() {const r = refresh.get(this.context.ebx.toString()); if (r) r.unclamped = this.context.eax.toInt32();}});
    hook(0x05a5c0, {onEnter(args) {if (active) out('begin-target', {c: counter(), caller: caller(this), self: words(this.context.ecx.add(8), 2),
        target: words(args[0].add(8), 2), range: args[1].isNull() ? null : u32(args[1]), a2: args[2].toUInt32(), a3: args[3].toUInt32(),
        persistent: args[5].toUInt32(), a6: args[6].toUInt32(), a7: args[7].toUInt32()});}});
    hook(0x5fc640, {onEnter(args) {if (active) out('begin-task', {c: counter(), caller: caller(this), move: this.context.ecx.toString(),
        target: args[0].toString(), range: args[1].isNull() ? null : u32(args[1]), persistent: args[2].toUInt32(), a3: args[3].toUInt32()});}});
    hook(0x5fb940, {
        onEnter(args) {this.t = args[0]; this.row = active ? {c: counter(), caller: caller(this), move: this.context.ecx.toString(), t: args[0].toString(),
            tw: args[0].isNull() ? null : safe(() => [u32(args[0].add(0x20)), u32(args[0].add(0x5c))])} : null;},
        onLeave(ret) {if (this.row) out('validate', {...this.row, result: ret.toUInt32()});}
    });
    hook(0x651010, {onEnter(args) {if (active) out('target-lost', {c: counter(), caller: caller(this), unit: this.context.ecx.toString(),
        a0: args[0].toUInt32(), a1: args[1].toUInt32(), w20: safe(() => u32(this.context.ecx.add(0x20))), w5c: safe(() => u32(this.context.ecx.add(0x5c)))});}});
    hook(0x5ff490, {
        onEnter(args) {this.row = active ? {c: counter(), caller: caller(this), move: this.context.ecx.toString(), ev: args[0].toString(),
            code: safe(() => u32(args[0].add(8))), t: safe(() => args[0].add(0xc).readPointer().toString())} : null;},
        onLeave() {if (this.row) out('on-target-lost', this.row);}
    });
    hook(0x16d9d0, {onEnter(args) {if (active) out('set-target', {c: counter(), caller: caller(this), g: this.context.ecx.toString(),
        t: args[0].toString(), tid: args[0].isNull() ? null : words(args[0].add(0x14), 2)});}});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {flush(); recording = false; return {installed, counts};}};
