// GROUP-03.4.7.3 read-only observer for WC3 1.27.1.7085 game.dll. No calls into game code, no writes.
// Markers: Preload string intern 231df0 ("RSH ...", from the map and the AI thread).  Hooks the
// attack-captain policy producers: SetHome 9d5c70 -> home re-evaluation 9d08e0 (+ target/engagement
// test 9d73c0), GoHome 9d2670, empty-roster actor placement 9d6ed0, point request 9d44d0,
// CaptainAttack 9d1680, member removal 9d7760, roster attach/detach 9cf680/9d5610, member reissue
// 9d87d0 and CaptainRetreating 9b8b50.  At every JASS sample marker it snapshots each seen captain
// (state/flags/counts/points/targets and its virtual actor mover through bridge+44) and each roster
// member's physical mover.
let installed = false, recording = true, sampleTick = -1;
const counts = {};
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = k => {counts[k] = (counts[k] || 0) + 1;};
const u32 = p => p.readU32() >>> 0;
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32() >>> 0);
const captains = new Map(), members = new Map();

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    emit('policy-constants', {retreatSpeed: base.add(0xd3c7e0).readU32(), emptySpeed: base.add(0xd3c804).readU32(), memberFactor: base.add(0xd77fb4).readU32(), baseFactor: base.add(0xd77fb8).readU32()});
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);
    const rel = p => p.sub(base).toUInt32();
    const clock = () => {const o = at(0xd53a48).readPointer(); return {counter: u32(o.add(0x538))};};
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
        return {mover: m.toString(), position: ints(m.add(0x78), 2), velocity: ints(m.add(0x80), 2), radius: u32(m.add(0x90)),
            storedRange: u32(m.add(0xb0)), group: ints(m.add(0x9c), 2), flags: u32(m.add(0xd8))};
    };
    const captainWords = c => ({captain: c.toString(), state: u32(c.add(0x64)), player: u32(c.add(0x68)), flags: u32(c.add(0x6c)),
        order: u32(c.add(0x70)), counts: ints(c.add(0xb8), 6).map(v => v | 0), current: ints(c.add(0x58), 4), request: [u32(c.add(0xd4)), u32(c.add(0xdc))],
        requestRange: u32(c.add(0xe4)), home: [u32(c.add(0xfc)), u32(c.add(0x104))],
        targets: [ints(c.add(0x108), 2), ints(c.add(0x114), 2), ints(c.add(0x120), 2)], roster: ints(c.add(0xac), 2)});
    const unitWords = u => ({unit: u.toString(), rawcode: u32(u.add(0x30)), order: ints(u.add(0x19c), 2), mover: mover(u.add(0x164))});
    const snapshot = reason => {
        const cs = [], ms = [];
        for (const c of captains.values()) {try {cs.push({...captainWords(c), actor: mover(c.add(0x44))});} catch (e) {cs.push({captain: c.toString(), error: String(e)});}}
        for (const u of members.values()) {try {ms.push(unitWords(u));} catch (e) {ms.push({unit: u.toString(), error: String(e)});}}
        emit('snapshot', {reason, ...clock(), captains: cs, members: ms});
    };
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value.startsWith('RSH ')) return;
        emit('marker', {value, ...clock()});
        const tick = parseInt((value.match(/tick=(\d+)/) || [])[1] || '-1');
        if ((value.includes(' label=sample ') && tick !== sampleTick) || value.includes(' label=complete') || value.includes(' label=command') || value.includes(' label=removed')) {
            if (tick >= 0) sampleTick = tick;
            snapshot(value);
        }
    }});
    const enterLeave = (rva, name, extra) => hook(rva, {
        onEnter(args) {this.c = this.context.ecx; captains.set(this.c.toString(), this.c); bump(name);
            this.row = {caller: rel(this.returnAddress), ...clock(), before: captainWords(this.c), ...(extra ? extra(this, args) : {})};},
        onLeave(ret) {emit('captain-call', {name, ...this.row, result: ret.toUInt32(), after: captainWords(this.c)});}
    });
    hook(0x9d4c20, {onEnter(args) {this.c=this.context.ecx;this.out=args[0];}, onLeave() {emit('captain-speed',{captain:this.c.toString(),word:u32(this.out),...clock()});}});
    enterLeave(0x9d7b00, 'periodic-update');
    enterLeave(0x9d5c70, 'set-home', (t, a) => ({x: u32(a[0]), y: u32(a[1])}));
    enterLeave(0x9d08e0, 'home-reevaluate');
    enterLeave(0x9d2670, 'go-home');
    enterLeave(0x9d1680, 'captain-attack', (t, a) => ({x: u32(a[0]), y: u32(a[1])}));
    enterLeave(0x9d44d0, 'point-request', (t, a) => ({x: u32(a[0]), y: u32(a[1]), retain: a[2].toUInt32(), range: u32(a[3]), prepare: a[4].toUInt32()}));
    enterLeave(0x9d7760, 'member-removed', (t, a) => ({unit: a[0].toString()}));
    hook(0x9d73c0, {onEnter() {this.c = this.context.ecx;}, onLeave(ret) {bump('engagement'); emit('engagement-test', {captain: this.c.toString(), result: ret.toUInt32(), ...clock()});}});
    hook(0x9d6ed0, {onEnter(args) {bump('actor-place'); emit('actor-place', {captain: this.context.ecx.toString(), x: u32(args[0].add(4)), y: u32(args[1].add(4)), caller: rel(this.returnAddress), ...clock()});}});
    for (const [name, rva] of [['attach', 0x9cf680], ['detach', 0x9d5610]]) hook(rva, {onEnter(args) {
        const u = args[0];
        members.set(u.toString(), u); captains.set(this.context.ecx.toString(), this.context.ecx);
        bump('roster-' + name);
        emit('roster', {name, caller: rel(this.returnAddress), ...clock(), captain: captainWords(this.context.ecx), ...unitWords(u)});
    }});
    hook(0x9d87d0, {onEnter(args) {
        bump('reissue');
        emit('reissue', {captain: this.context.ecx.toString(), unit: args[0].toString(), order: args[1].toUInt32(), target: args[2].toString(),
            point: [u32(args[3]), u32(args[4])], caller: rel(this.returnAddress), ...clock()});
    }});
    let lastRetreat = -1;
    hook(0x9b8b50, {onLeave(ret) {bump('retreating'); const v = ret.toUInt32(); if (v !== lastRetreat) {lastRetreat = v; emit('retreating-native', {result: v, ...clock(), tick: sampleTick});}}});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {recording = false; return {installed, counts};}};
