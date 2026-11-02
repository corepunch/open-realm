// ORDER-03.1 / ORDER-03.2 read-only observer for WC3 1.27.1.7085 game.dll.
// Entry/exit hooks only: reads memory, never calls game code, never writes target data.
// `config` is prepended by order03_trace.py.
let installed = false, recording = true, base = null;
const counts = {}, caps = {};
const emit = (event, data = {}) => {
    if (!recording) return;
    counts[event] = (counts[event] || 0) + 1;
    const cap = config.caps[event] ?? config.caps.default;
    if (counts[event] > cap) { caps[event] = (caps[event] || 0) + 1; return; }
    send({event, seq: seqNo++, ...data});
};
let seqNo = 0;
const hex = v => (v >>> 0).toString(16).padStart(8, '0');
const u32 = p => p.readU32() >>> 0;
const JASS_LO = 0x80200, JASS_HI = 0x80400;
const VT = {0xb77eb0: 'CUnit', 0xaa238c: 'CUnitEventReg', 0xaa3310: 'CTriggerWar3', 0xa9f708: 'CPlayerWar3',
    0xaa1450: 'CEventRegWar3', 0xaa1e08: 'CPlayerEventReg', 0xaa17ac: 'CDeathEventReg'};
let tick = 0, phase = 0, lastMarker = null, pendingRegName = null;
const watchedTables = new Map();   // table -> {agent, name}
const agentNames = new Map();      // agent -> name
const regNames = new Map();        // reg -> {name, trigger, unit, event}
const triggerNames = new Map();    // trigger -> name

function rva(p) {
    try {
        const v = p.toUInt32 ? p.toUInt32() : p;
        const b = base.toUInt32();
        return (v >= b && v < b + config.imageSize) ? v - b : null;
    } catch (e) { return null; }
}
function vtName(obj) {
    if (obj.isNull()) return null;
    try { const r = rva(obj.readPointer()); return r === null ? 'heap' : (VT[r] || r.toString(16)); } catch (e) { return 'unreadable'; }
}
function objText(obj) {
    if (obj.isNull()) return null;
    const k = obj.toString();
    return {p: k, vt: vtName(obj), refs: safeU32(obj.add(4)),
        name: agentNames.get(k) || (regNames.get(k) || {}).name || triggerNames.get(k) || null};
}
function safeU32(p) { try { return u32(p); } catch (e) { return null; } }
function header(table) {
    if (table.isNull()) return null;
    return {t: table.toString(), depth: table.readU8(), buckets: table.add(1).readU8(), count: table.add(2).readU16(),
        array: table.add(4).readPointer().toString()};
}
function node(n, sentinel) {
    const cb = n.add(8).readPointer();
    return {n: n.toString(), sentinel: sentinel !== null && n.equals(sentinel), ev: hex(u32(n.add(4))),
        cb: cb.isNull() ? null : objText(cb), remap: hex(u32(n.add(0xc)))};
}
function bucketChain(table, index, sentinel) {
    const arr = table.add(4).readPointer();
    const tail = arr.add(4 * index).readPointer();
    const out = [];
    if (tail.isNull()) return out;
    let n = tail.readPointer();
    for (let i = 0; i < 96; i++) {
        out.push(node(n, sentinel));
        if (n.equals(tail)) return out;
        n = n.readPointer();
    }
    out.push({truncated: true});
    return out;
}
function chainFor(table, ev, sentinel = null) {
    if (table.isNull()) return null;
    const n = table.add(1).readU8();
    if (n === 0) return [];
    return bucketChain(table, ev & (n - 1), sentinel);
}
function allChains(table) {
    if (table.isNull()) return null;
    const n = table.add(1).readU8(), out = [];
    for (let i = 0; i < n; i++) out.push(bucketChain(table, i, null));
    return out;
}
function agentState(agent) {
    const table = agent.add(8).readPointer();
    return {agent: objText(agent), table: header(table)};
}
function watched() {
    const out = [];
    for (const [t, w] of watchedTables) {
        try { out.push({name: w.name, ...header(ptr(t))}); } catch (e) { out.push({name: w.name, t, error: String(e)}); }
    }
    return out;
}
function watchAgent(agent, name) {
    const table = agent.add(8).readPointer();
    agentNames.set(agent.toString(), name);
    if (!table.isNull()) watchedTables.set(table.toString(), {agent: agent.toString(), name});
}
function isWatchedAgent(agent) { return agentNames.has(agent.toString()); }
function inJass(ev) { const e = ev >>> 0; return e >= JASS_LO && e < JASS_HI; }

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

    // JASS markers (Preload string intern; same site as wc3_pathfinding.js).
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        let value;
        try { value = args[0].readCString(); } catch (e) { return; }
        if (!value || !value.startsWith('RSO3 ')) return;
        const t = /tick=(\d+)/.exec(value); if (t) tick = parseInt(t[1]);
        const p = / phase=(\d+)/.exec(value); if (p) phase = parseInt(p[1]);
        const r = / reg begin trig=([A-Za-z0-9]+)/.exec(value);
        pendingRegName = r ? r[1] : (value.includes(' reg end ') ? null : pendingRegName);
        lastMarker = value;
        emit('marker', {value, watched: watched()});
    }});

    // Unit-event registration object init: ECX reg; trigger, unit, event, filter.
    hook(0x27aa90, {onEnter(args) {
        const reg = this.context.ecx, trig = args[0], unit = args[1], ev = args[2].toUInt32();
        const name = pendingRegName || '?';
        regNames.set(reg.toString(), {name: name + '.reg', trigger: trig.toString(), unit: unit.toString(), event: hex(ev)});
        triggerNames.set(trig.toString(), name);
        if (!isWatchedAgent(unit)) agentNames.set(unit.toString(), 'unit@' + tick);
        this.reg = reg; this.unit = unit; this.ev = ev; this.name = name;
        this.before = agentState(unit);
    }, onLeave() {
        watchAgent(this.unit, agentNames.get(this.unit.toString()));
        const table = this.unit.add(8).readPointer();
        emit('unit-reg', {name: this.name, reg: objText(this.reg), unit: this.unit.toString(), ev: hex(this.ev),
            before: this.before, after: agentState(this.unit), chain: chainFor(table, this.ev), tick, phase});
    }});
    // Player-unit event registration init (27a530): ECX reg; trigger, player, event, filter.
    hook(0x27a530, {onEnter(args) {
        const reg = this.context.ecx, trig = args[0], player = args[1], ev = args[2].toUInt32();
        const name = pendingRegName || '?';
        regNames.set(reg.toString(), {name: name + '.reg', trigger: trig.toString(), unit: player.toString(), event: hex(ev)});
        triggerNames.set(trig.toString(), name);
        this.reg = reg; this.player = player; this.ev = ev; this.name = name;
    }, onLeave() {
        watchAgent(this.player, 'player0');
        const table = this.player.add(8).readPointer();
        emit('player-reg', {name: this.name, reg: objText(this.reg), player: this.player.toString(), ev: hex(this.ev),
            after: agentState(this.player), chain: chainFor(table, this.ev), tick, phase});
    }});

    // Payoff178: actual immediate event producer callers, with no game calls.
    hook(0x67bd10, {onEnter(args) {
        emit('immediate-producer', {unit: objText(this.context.ecx),
            command: safeU32(args[0].add(0x24)), tick, phase,
            backtrace: Thread.backtrace(this.context, Backtracer.ACCURATE).map(rva)});
    }});
    // Agent dispatch wrapper (vtable+14): ECX agent; event, packet. Holds agent ref across 071e00.
    hook(0x071dc0, {onEnter(args) {
        const agent = this.context.ecx, ev = args[0].toUInt32();
        this.watch = inJass(ev) || isWatchedAgent(agent);
        if (!this.watch) return;
        this.agent = agent; this.ev = ev; this.packet = args[1];
        const table = agent.add(8).readPointer();
        this.table = table;
        emit('dispatch-enter', {agent: objText(agent), ev: hex(ev), packet: this.packet.toString(),
            packetWords: Array.from({length: 6}, (_, i) => hex(u32(this.packet.add(4 * i)))), packetVt: vtName(this.packet),
            table: header(table), chain: chainFor(table, ev), caller: caller(this), tick, phase});
    }, onLeave(ret) {
        if (!this.watch) return;
        const table = this.agent.add(8).readPointer();
        emit('dispatch-leave', {agent: objText(this.agent), ev: hex(this.ev), ret: ret.toUInt32(),
            packetWords: Array.from({length: 6}, (_, i) => hex(u32(this.packet.add(4 * i)))),
            table: header(table), tableChanged: !table.equals(this.table), chain: chainFor(table, this.ev), tick, phase});
    }});
    // Core dispatcher: record the stack sentinel address (EBP-3c = entry ESP-40).
    hook(0x071e00, {onEnter(args) {
        const table = this.context.ecx, ev = args[0].toUInt32();
        this.watch = inJass(ev) || watchedTables.has(table.toString());
        if (!this.watch) return;
        this.table = table; this.ev = ev;
        this.sentinel = this.context.esp.sub(0x40);
        emit('core-enter', {table: header(table), ev: hex(ev), sentinel: this.sentinel.toString(), tick, phase});
    }, onLeave(ret) {
        if (!this.watch) return;
        emit('core-leave', {table: header(this.table), ev: hex(this.ev), ret: ret.toUInt32(), chain: chainFor(this.table, this.ev),
            tick, phase});
    }});
    // Table-level register (0725d0), unregister (0728e0), clear (071bd0), grow (071a90).
    hook(0x0725d0, {onEnter(args) {
        const table = this.context.ecx, ev = args[0].toUInt32();
        this.watch = inJass(ev) || watchedTables.has(table.toString());
        if (!this.watch) return;
        this.table = table; this.ev = ev; this.remap = args[1].toUInt32(); this.cb = args[2];
        this.before = {header: header(table), chain: chainFor(table, ev)};
    }, onLeave() {
        if (!this.watch) return;
        emit('register', {ev: hex(this.ev), remap: hex(this.remap), cb: objText(this.cb), watched: watchedTables.get(this.table.toString()) || null,
            before: this.before, after: {header: header(this.table), chain: chainFor(this.table, this.ev)}, caller: caller(this), tick, phase});
    }});
    hook(0x0728e0, {onEnter(args) {
        const table = this.context.ecx, ev = args[0].toUInt32();
        this.watch = inJass(ev) || watchedTables.has(table.toString());
        if (!this.watch) return;
        this.table = table; this.ev = ev; this.cb = args[1];
        this.cbText = this.cb.isNull() ? null : objText(this.cb);
        this.before = {header: header(table), chain: chainFor(table, ev)};
        this.where = caller(this);
        this.bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 8).map(a => { const r = rva(a); return r === null ? 'ext' : r.toString(16); });
    }, onLeave() {
        if (!this.watch) return;
        emit('unregister', {ev: hex(this.ev), cb: this.cbText, cbRefsAfter: this.cb.isNull() ? null : safeU32(this.cb.add(4)),
            watched: watchedTables.get(this.table.toString()) || null,
            before: this.before, after: {header: header(this.table), chain: chainFor(this.table, this.ev)}, caller: this.where,
            backtrace: this.bt, tick, phase});
    }});
    hook(0x071d00, {onEnter() {
        const agent = this.context.ecx;
        this.watch = isWatchedAgent(agent);
        if (!this.watch) return;
        this.agent = agent; this.table = agent.add(8).readPointer();
        this.before = {header: header(this.table), chains: allChains(this.table)};
        this.bt = Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 10).map(a => { const r = rva(a); return r === null ? 'ext' : r.toString(16); });
    }, onLeave() {
        if (!this.watch) return;
        const now = this.agent.add(8).readPointer();
        emit('clear-subscriptions', {agent: objText(this.agent), table: this.table.toString(), tableAfter: now.toString(),
            before: this.before, after: now.isNull() ? null : {header: header(now), chains: allChains(now)}, backtrace: this.bt, tick, phase});
    }});
    hook(0x071a90, {onEnter() {
        const table = this.context.ecx;
        this.watch = watchedTables.has(table.toString());
        if (!this.watch) return;
        this.table = table;
        this.before = {header: header(table), chains: allChains(table)};
        this.where = caller(this);
    }, onLeave() {
        if (!this.watch) return;
        emit('grow', {watched: watchedTables.get(this.table.toString()), before: this.before,
            after: {header: header(this.table), chains: allChains(this.table)}, caller: this.where, tick, phase});
    }});
    // Subscriber handlers.
    hook(0x27d1f0, {onEnter(args) {
        const reg = this.context.ecx, packet = args[0];
        this.reg = reg; this.packetEv = u32(packet.add(8));
        emit('reg-handler', {reg: objText(reg), regEvent: hex(u32(reg.add(0x34))), packetEv: hex(this.packetEv), tick, phase});
    }, onLeave(ret) { emit('reg-handler-leave', {reg: this.reg.toString(), ret: ret.toUInt32(), regRefs: safeU32(this.reg.add(4))}); }});
    hook(0x298d20, {onEnter(args) {
        const trig = this.context.ecx, packet = args[0];
        this.trig = trig;
        emit('trigger-handler', {trigger: objText(trig), packetEv: hex(u32(packet.add(8))), eval: safeU32(trig.add(0x58)),
            exec: safeU32(trig.add(0x5c)), tick, phase});
    }, onLeave(ret) { emit('trigger-handler-leave', {trigger: this.trig.toString(), ret: ret.toUInt32(), eval: safeU32(this.trig.add(0x58)), exec: safeU32(this.trig.add(0x5c))}); }});
    // Lifetime: deferred order/agent release request (virtual 5c), wrapper destroy, payload release, destructors.
    hook(0x0557b0, {onEnter() {
        const o = this.context.ecx;
        this.o = o; this.wrapperReq = null;
        emit('release-request', {obj: objText(o), id: [safeU32(o.add(0xc)), safeU32(o.add(0x10))], caller: caller(this), tick, phase});
    }});
    hook(0x145c70, {onEnter() {
        const w = this.context.ecx;
        const payload = w.add(0x54).readPointer();
        emit('wrapper-destroy', {wrapper: w.toString(), payload: payload.isNull() ? null : objText(payload), tick, phase});
    }});
    for (const [r, name] of [[0x2759f0, 'CUnitEventReg'], [0x2960f0, 'CTriggerWar3'], [0x667d60, 'CUnit']]) hook(r, {onEnter(args) {
        const o = this.context.ecx;
        emit('destructor', {cls: name, obj: o.toString(), name: agentNames.get(o.toString()) || (regNames.get(o.toString()) || {}).name ||
            triggerNames.get(o.toString()) || null, refs: safeU32(o.add(4)), table: o.add(8).readPointer().toString(), caller: caller(this), tick, phase});
    }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {
    status() { return {installed, counts, caps}; },
    finish() { recording = false; return {installed, counts, caps, agentNames: Object.fromEntries(agentNames),
        regNames: Object.fromEntries(regNames), triggerNames: Object.fromEntries(triggerNames)}; }
};
