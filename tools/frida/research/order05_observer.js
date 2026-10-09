// ORDER-05.1 / 05.2 / 05.3 read-only observer (agent request clocks) for WC3 1.27.1.7085 game.dll.
// Entry/exit hooks only: reads memory, never calls game code, never writes target data.
// `config` is prepended by order_trace.py.
let installed = false, recording = true, base = null, seqNo = 0;
const counts = {}, caps = {};
const emit = (event, data = {}) => {
    if (!recording) return;
    counts[event] = (counts[event] || 0) + 1;
    const cap = config.caps[event] ?? config.caps.default;
    if (counts[event] > cap) { caps[event] = (caps[event] || 0) + 1; return; }
    send({event, seq: seqNo++, ...data});
};
const hex = v => (v >>> 0).toString(16).padStart(8, '0');
const safeU32 = p => { try { return p.readU32() >>> 0; } catch (e) { return null; } };
const f32 = p => { try { return p.readFloat(); } catch (e) { return null; } };
let tick = 0, phase = 0, win = false, inRangeSetPeriod = 0, inLoad = 0, inSave = 0;
const watched = new Map();        // receiver -> label
const lastFlags = {};

function rva(p) {
    try {
        const v = p.toUInt32 ? p.toUInt32() : p;
        const b = base.toUInt32();
        return (v >= b && v < b + config.imageSize) ? v - b : null;
    } catch (e) { return null; }
}
function vtName(obj) {
    if (obj.isNull()) return null;
    try { const r = rva(obj.readPointer()); return r === null ? 'heap' : r.toString(16); } catch (e) { return 'unreadable'; }
}
function owner() { return base.add(0xd53a48).readPointer(); }
function clockId(clock) {
    try {
        const o = owner();
        if (clock.equals(o.add(0x14))) return 'primary';
        if (clock.equals(o.add(0x68))) return 'presentation';
    } catch (e) { }
    return clock.toString();
}
function clockState(clock) {
    return {id: clockId(clock), p: clock.toString(), time: f32(clock.add(0x40)), timeW: hex(safeU32(clock.add(0x40))),
        epoch: safeU32(clock.add(0x44)), span: f32(clock.add(0x48)), flags: hex(safeU32(clock.add(0x4c))),
        serial: safeU32(clock.add(0x50)), count: safeU32(clock.add(0x20)), live: safeU32(clock.add(0x3c))};
}
function clocks() {
    try { const o = owner(); if (o.isNull()) return {owner: null}; return {owner: o.toString(), primary: clockState(o.add(0x14)), presentation: clockState(o.add(0x68))}; }
    catch (e) { return {error: String(e)}; }
}
function wrapperText(w) {
    if (w.isNull()) return null;
    return {p: w.toString(), vt: vtName(w), id: [hex(safeU32(w.add(0x14))), hex(safeU32(w.add(0x18)))], timer: hex(safeU32(w.add(0x1c))),
        release: hex(safeU32(w.add(0x20))), flags: hex(safeU32(w.add(0x4c))), label: watched.get(w.toString()) || null};
}
function req(r) {
    if (r.isNull()) return null;
    const recv = r.add(0x18).readPointer();
    return {r: r.toString(), deadline: f32(r.add(4)), deadlineW: hex(safeU32(r.add(4))), delay: f32(r.add(8)), delayW: hex(safeU32(r.add(8))),
        clock: clockId(r.add(0xc).readPointer()), flags: hex(safeU32(r.add(0x10))), serial: safeU32(r.add(0x14)),
        recv: recv.toString(), recvVt: vtName(recv), recvLabel: watched.get(recv.toString()) || null, value: hex(safeU32(r.add(0x1c)))};
}
function heap(clock, limit) {
    const n = safeU32(clock.add(0x20)), arr = clock.add(0x10).readPointer(), out = [];
    for (let i = 1; i < n && i <= limit; i++) out.push(req(arr.add(4 * i).readPointer()));
    return {count: n, items: out, truncated: n - 1 > limit};
}
function interesting(recv) { return win || watched.has(recv.toString()) || inLoad > 0 || inSave > 0; }

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    base = module.base;
    const pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const hook = (r, callbacks) => Interceptor.attach(base.add(r), callbacks);
    const caller = ctx => { const r = rva(ctx.returnAddress); return r === null ? 'ext' : r.toString(16); };

    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        let value;
        try { value = args[0].readCString(); } catch (e) { return; }
        if (!value || !value.startsWith('RSO5 ')) return;
        const t = /tick=(\d+)/.exec(value); if (t) tick = parseInt(t[1]);
        const p = / phase=(\d+)/.exec(value); if (p) phase = parseInt(p[1]);
        if (value.includes(' win=1')) win = true;
        emit('marker', {value, win, clocks: clocks()});
        if (value.includes(' win=0')) win = false;
    }});
    // Queue (all producers): ECX clock; receiver, &value, delay, &deadline, serial. EAX = request.
    hook(0x15e310, {onEnter(args) {
        this.recv = args[0];
        this.log = interesting(this.recv);
        if (this.log) { this.clock = this.context.ecx; this.where = caller(this); }
    }, onLeave(ret) {
        if (!this.log) return;
        emit('queue', {req: req(ret), by: this.where, recv: wrapperText(this.recv), clock: clockState(this.clock), tick, phase});
    }});
    hook(0x054370, {onEnter() {
        const r = this.context.ecx, recv = r.add(0x18).readPointer();
        this.log = interesting(recv);
        if (!this.log) return;
        this.r = r;
        emit('execute', {req: req(r), clockTime: f32(r.add(0xc).readPointer().add(0x40)), tick, phase});
    }, onLeave() {
        if (!this.log) return;
        emit('execute-leave', {r: this.r.toString(), flagsNow: hex(safeU32(this.r.add(0x10))), tick, phase});
    }});
    hook(0x053710, {onEnter(args) {
        const r = args[0];
        this.log = interesting(r.add(0x18).readPointer());
        if (this.log) this.r = r;
    }, onLeave() { if (this.log) emit('rearm', {req: req(this.r), tick, phase}); }});
    hook(0x0521f0, {onEnter() {
        this.clock = this.context.ecx;
        emit('rebase-enter', {clock: clockState(this.clock), heap: heap(this.clock, 48), tick, phase});
    }, onLeave() { emit('rebase-leave', {clock: clockState(this.clock), heap: heap(this.clock, 48), tick, phase}); }});
    // Advance (fastcall ECX &increment, EDX clock): emit only on flag changes and near the span.
    hook(0x054190, {onEnter() {
        const clock = this.context.edx, inc = f32(this.context.ecx);
        const k = clock.toString(), flags = safeU32(clock.add(0x4c)), time = f32(clock.add(0x40)), span = f32(clock.add(0x48));
        if (lastFlags[k] !== flags) { emit('clock-flags', {clock: clockState(clock), before: lastFlags[k] === undefined ? null : hex(lastFlags[k]), inc, tick, phase}); lastFlags[k] = flags; }
        this.near = time !== null && span !== null && time + inc >= span - 0.05;
        if (this.near) { this.clock = clock; emit('advance-near-span', {clock: clockState(clock), inc, incW: hex(safeU32(this.context.ecx)), tick, phase}); }
    }, onLeave() { if (this.near) emit('advance-near-span-leave', {clock: clockState(this.clock), tick, phase}); }});
    // Release / timers.
    hook(0x15e0e0, {onEnter() {
        const w = this.context.ecx;
        this.log = win || watched.has(w.toString());
        if (!this.log) return;
        this.w = w; this.before = wrapperText(w); this.where = caller(this);
    }, onLeave() { if (this.log) emit('release', {before: this.before, after: wrapperText(this.w), by: this.where, tick, phase}); }});
    hook(0x15fe60, {onEnter() {
        inRangeSetPeriod++;
        const l = this.context.ecx;
        watched.set(l.toString(), 'range@' + tick);
        emit('range-set-period', {listener: wrapperText(l), by: caller(this), tick, phase});
    }, onLeave() { inRangeSetPeriod--; }});
    hook(0x15d6f0, {onEnter(args) {
        const w = this.context.ecx;
        if (inRangeSetPeriod > 0 && !watched.has(w.toString())) watched.set(w.toString(), 'range@' + tick);
        this.log = win || watched.has(w.toString());
        if (!this.log) return;
        this.w = w; this.before = wrapperText(w); this.period = f32(args[0]); this.value = hex(args[1].toUInt32()); this.where = caller(this);
    }, onLeave() {
        if (!this.log) return;
        const t = this.w.add(0x1c).readPointer();
        emit('start-timer', {before: this.before, after: wrapperText(this.w), period: this.period, value: this.value, req: req(t), by: this.where, tick, phase});
    }});
    hook(0x15d7a0, {onEnter() {
        const w = this.context.ecx;
        this.log = win || watched.has(w.toString());
        if (!this.log) return;
        const t = w.add(0x1c).readPointer();
        emit('stop-timer', {wrapper: wrapperText(w), req: req(t), by: caller(this), tick, phase});
    }});
    hook(0x15e500, {onEnter(args) {
        const w = this.context.ecx;
        this.log = win || watched.has(w.toString());
        if (!this.log) return;
        emit('on-clock-request', {wrapper: wrapperText(w), r: args[0].toString(), isRelease: args[0].equals(w.add(0x20).readPointer()),
            isTimer: args[0].equals(w.add(0x1c).readPointer()), tick, phase});
    }});
    hook(0x145c70, {onEnter() {
        const w = this.context.ecx;
        if (!(win || watched.has(w.toString()))) return;
        emit('wrapper-destroy', {wrapper: wrapperText(w), tick, phase});
    }});
    hook(0x15fdc0, {onEnter() { emit('range-emit-enter', {listener: wrapperText(this.context.ecx), clocks: clocks(), tick, phase}); }});
    hook(0x05c020, {onEnter() { this.where = caller(this); }, onLeave(ret) {
        emit('range-create', {ret: ret.toString(), by: this.where, tick, phase});
    }});
    // Save / load of agent requests and the game load entry.
    hook(0x15de50, {onEnter() {
        const r = this.context.edx;
        inSave++;
        if (!r.isNull() && watched.has(r.add(0x18).readPointer().toString())) emit('request-save', {req: req(r), clocks: clocks(), tick, phase});
        else counts['request-save-other'] = (counts['request-save-other'] || 0) + 1;
    }, onLeave() { inSave--; }});
    hook(0x15dd30, {onEnter() { this.w = this.context.ecx; }, onLeave() {
        const w = this.w;
        const t = w.add(0x1c).readPointer(), rl = w.add(0x20).readPointer();
        if (!t.isNull() || !rl.isNull()) emit('wrapper-load', {wrapper: wrapperText(w), timer: req(t), release: req(rl), clocks: clocks(), tick, phase});
        else counts['wrapper-load-idle'] = (counts['wrapper-load-idle'] || 0) + 1;
    }});
    hook(0x04ced0, {onEnter() { inLoad++; emit('game-load-enter', {clocks: clocks(), tick, phase}); },
        onLeave(ret) { inLoad--; emit('game-load-leave', {ret: ret.toUInt32(), clocks: clocks(), tick, phase}); }});
    hook(0x053110, {onEnter(args) { this.a = [args[0].toUInt32(), args[1].toString()]; emit('settle-enter', {args: this.a, clocks: clocks(), by: caller(this), tick, phase}); },
        onLeave() { emit('settle-leave', {args: this.a, clocks: clocks(), tick, phase}); }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {
    status() { return {installed, counts, caps}; },
    finish() { recording = false; return {installed, counts, caps, watched: Object.fromEntries(watched), clocks: base ? clocks() : null}; }
};
