// GROUP-03.4.7.3 retreat473 read-only observer for WC3 1.27.1.7085 game.dll.
// No NativeFunction, game invocation, replacement or memory write. Hooks only read memory.
// Records every "R473 " Preload marker with the raw GetUnitX/GetUnitY return words of its
// unit row, snapshots every observed attack Captain (policy fields and virtual actor mover)
// at each label marker, and records the Captain policy producers recovered by the
// GROUP-03.4.7.3 handoff (offsets/ABIs from that handoff and Payoff159).
let installed = false, recording = true, tick = -1, seq = 0;
const counts = {}, query = [0, 0];
const captains = new Map();
const u32 = p => p.readU32() >>> 0;
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32() >>> 0);
const emit = (event, data = {}) => {counts[event] = (counts[event] || 0) + 1; if (recording) send({event, seq: ++seq, tick, ...data});};

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);
    const rel = p => p.sub(base).toUInt32();
    const counter = () => u32(at(0xd53a48).readPointer().add(0x538));
    const primary = () => ints(at(0xd53a48).readPointer().add(0x54), 3);
    const mover = bridge => {
        const id = u32(bridge.add(8)), epoch = u32(bridge.add(0xc));
        if (id === 0xffffffff) return null;
        const registry = at(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
        const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
        if (index >= limit) return {error: 'index'};
        const slot = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index * 8);
        if (slot.readS32() !== -2) return {error: 'dead'};
        const m = slot.add(4).readPointer();
        if (u32(m.add(0x18)) !== epoch) return {error: 'epoch'};
        return {position: ints(m.add(0x78), 2), velocity: ints(m.add(0x80), 2), maximum: u32(m.add(0x88)),
            heading: u32(m.add(0x8c)), radius: u32(m.add(0x90)), storedRange: u32(m.add(0xb0)), group: ints(m.add(0x9c), 2), flags: u32(m.add(0xd8))};
    };
    const captainWords = c => ({captain: c.toString(), state: u32(c.add(0x64)), flags: u32(c.add(0x6c)), order: u32(c.add(0x70)),
        counts: ints(c.add(0xb8), 6).map(v => v | 0), current: ints(c.add(0x58), 4), request: [u32(c.add(0xd4)), u32(c.add(0xdc))],
        requestRange: u32(c.add(0xe4)), home: [u32(c.add(0xfc)), u32(c.add(0x104))],
        targets: [ints(c.add(0x108), 2), ints(c.add(0x114), 2), ints(c.add(0x120), 2)]});
    const snapshot = () => {
        const out = [];
        for (const c of captains.values()) {
            try {out.push({...captainWords(c), actor: mover(c.add(0x44))});} catch (e) {out.push({captain: c.toString(), error: String(e)});}
        }
        return out;
    };
    hook(0x231df0, {onEnter(args) {
        if (!recording || args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value || !value.startsWith(config.prefix)) return;
        const m = /tick=(\d+) /.exec(value);
        if (!m) throw new Error('marker tick');
        tick = Number(m[1]);
        const unit = /label=unit u=(\d+)/.exec(value);
        if (unit) emit('public-position', {u: Number(unit[1]), xy: query.slice()});
        query[0] = query[1] = 0;
        emit('marker', {value, c: counter(), clock: primary(), ...(unit ? {} : {captains: snapshot()})});
    }});
    for (const [rva, axis] of [[0x204100, 0], [0x204140, 1]])
        hook(rva, {onLeave(ret) {if (recording) query[axis] = ret.toUInt32();}});
    const enterLeave = (rva, name, extra) => hook(rva, {
        onEnter(args) {this.c = this.context.ecx; captains.set(this.c.toString(), this.c);
            this.row = {caller: rel(this.returnAddress), c: counter(), clock: primary(), before: captainWords(this.c), ...(extra ? extra(args) : {})};},
        onLeave(ret) {emit('captain-call', {name, ...this.row, result: ret.toUInt32(), after: captainWords(this.c), actor: mover(this.c.add(0x44))});}
    });
    enterLeave(0x9d5c70, 'set-home', a => ({x: u32(a[0]), y: u32(a[1])}));
    enterLeave(0x9d08e0, 'home-reevaluate');
    enterLeave(0x9d2670, 'go-home');
    enterLeave(0x9d1680, 'captain-attack', a => ({x: u32(a[0]), y: u32(a[1])}));
    enterLeave(0x9d44d0, 'point-request', a => ({x: u32(a[0]), y: u32(a[1]), retain: a[2].toUInt32(), range: u32(a[3]), prepare: a[4].toUInt32()}));
    enterLeave(0x9d7760, 'member-removed', a => ({unit: a[0].toString()}));
    enterLeave(0x9cfeb0, 'goal-event');
    enterLeave(0x9d7b00, 'periodic-update');
    hook(0x9d73c0, {onEnter() {this.c = this.context.ecx;}, onLeave(ret) {emit('engagement-test', {captain: this.c.toString(), result: ret.toUInt32(), c: counter()});}});
    hook(0x9d6ed0, {onEnter(args) {emit('actor-place', {captain: this.context.ecx.toString(), x: u32(args[0].add(4)), y: u32(args[1].add(4)), caller: rel(this.returnAddress), c: counter()});}});
    for (const [name, rva] of [['attach', 0x9cf680], ['detach', 0x9d5610]]) hook(rva, {onEnter(args) {
        captains.set(this.context.ecx.toString(), this.context.ecx);
        emit('roster', {name, caller: rel(this.returnAddress), c: counter(), unit: args[0].toString(), captain: captainWords(this.context.ecx)});
    }});
    hook(0x9d87d0, {onEnter(args) {
        emit('reissue', {captain: this.context.ecx.toString(), unit: args[0].toString(), order: args[1].toUInt32(), target: args[2].toString(),
            point: [u32(args[3]), u32(args[4])], caller: rel(this.returnAddress), c: counter()});
    }});
    let lastRetreat = -1;
    hook(0x9b8b50, {onLeave(ret) {const v = ret.toUInt32(); if (v !== lastRetreat) {lastRetreat = v; emit('retreating-native', {result: v, c: counter()});}}});
    emit('module', {base: base.toString(), path: module.path});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() {recording = false; return {installed, counts, seq, readOnly: true};}};
