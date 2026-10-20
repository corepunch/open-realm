// GROUP-03.4.6.2.1.2 read-only observer for WC3 1.27.1.7085 game.dll. No calls into game code, no writes.
// Markers: Preload string intern 231df0 ("RSG ...").  Hooks the private Captain approach range
// 9d86f0 (caller, unit/captain/attack words, BTLF lookup result at 9d871e, enabled maximum 4985c0
// at 9d8743 and per-slot 499790 results), suppression counter mutation 497da0 (buff apply/remove),
// roster attach/detach 9cf680/9d5610, member reissue 9d87d0, Move task creation 6926b0 from 5fd92b
// and the resulting mover stored range (+b0) at marker samples.
let installed = false, recording = true, inRange = 0, sampleTick = -1;
const counts = {};
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = k => {counts[k] = (counts[k] || 0) + 1;};
const u32 = p => p.readU32() >>> 0;
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32() >>> 0);
const units = new Map(), captainsSeen = new Map();

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);
    const rel = p => p.sub(base).toUInt32();
    const attackWords = a => a.isNull() ? null : {attack: a.toString(), vtable: rel(a.readPointer()), flags20: u32(a.add(0x20)),
        suppress: ints(a.add(0x224), 3).map(v => v | 0), weaponTypes: ints(a.add(0xdc), 2), damageTypes: ints(a.add(0xf4), 2),
        targets: ints(a.add(0x218), 2), ranges: [u32(a.add(0x258)), u32(a.add(0x260))], slot2b8: u32(a.add(0x2b8)),
        target: ints(a.add(0x2bc), 2), adjusted: u32(a.add(0x2cc)), adjustedVtable: rel(a.add(0x2c8).readPointer())};
    const mover = unit => {
        const id = u32(unit.add(0x164 + 8)), epoch = u32(unit.add(0x164 + 0xc));
        if (id === 0xffffffff) return null;
        const registry = at(0xd68610).readPointer(), alternate = (id & 0x80000000) !== 0;
        const index = id & 0x7fffffff, limit = registry.add(alternate ? 0x3c : 0x1c).readU32();
        if (index >= limit) return {error: 'index'};
        const slot = registry.add(alternate ? 0x2c : 0xc).readPointer().add(index * 8);
        if (slot.readS32() !== -2) return {error: 'dead'};
        const m = slot.add(4).readPointer();
        if (u32(m.add(0x18)) !== epoch) return {error: 'epoch'};
        return {mover: m.toString(), radius: u32(m.add(0x90)), storedRange: u32(m.add(0xb0)), position: ints(m.add(0x78), 2), flags: u32(m.add(0xd8))};
    };
    const unitWords = u => ({unit: u.toString(), rawcode: u32(u.add(0x30)), flags5c: u32(u.add(0x5c)),
        attackPtr: u.add(0x1e8).readPointer().toString(), mover: mover(u)});
    const snapshot = reason => {
        const rows = [];
        for (const [key, u] of units) {
            try {rows.push(unitWords(u));} catch (e) {rows.push({unit: key, error: String(e)});}
        }
        const caps = [];
        for (const c of captainsSeen.values()) {try {caps.push({captain: c.toString(), state: u32(c.add(0x64)), flags: u32(c.add(0x6c)), counts: ints(c.add(0xb8), 6).map(v => v | 0), actor: mover(c.add(0x44 - 0x164))});} catch (e) {caps.push({captain: c.toString(), error: String(e)});}}
        emit('unit-snapshot', {reason, rows, captains: caps});
    };
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value.startsWith('RSG ')) return;
        emit('marker', {value});
        const tick = parseInt((value.match(/tick=(\d+)/) || [])[1] || '-1');
        if ((value.includes(' label=sample ') && tick !== sampleTick) || value.includes(' label=complete')) {
            sampleTick = tick; snapshot(value);
        }
    }});
    hook(0x9d86f0, {
        onEnter(args) {
            inRange++;
            this.out = args[0]; this.unit = args[1]; this.captain = this.context.ecx; captainsSeen.set(this.captain.toString(), this.captain);
            const a = this.unit.add(0x1e8).readPointer();
            units.set(this.unit.toString(), this.unit);
            this.row = {caller: rel(this.returnAddress), captain: this.captain.toString(), captainFlags: u32(this.captain.add(0x6c)),
                captainState: u32(this.captain.add(0x64)), ...unitWords(this.unit), attackBefore: attackWords(a),
                constants: [0xd77fb0, 0xd77fc0, 0xd77fc4, 0xd3c7ac, 0xd3c7c8, 0xd3c744].map(r => u32(at(r))), sixTenths: u32(at(0xcd53fc))};
            this.trail = [];
            globalThis.rsgTrail = this.trail;
        },
        onLeave() {
            inRange--;
            bump('authored-range');
            emit('authored-range', {...this.row, trail: this.trail, range: u32(this.out)});
            globalThis.rsgTrail = null;
        }
    });
    // Function-level hooks only.  Exploratory capture c used instruction probes at 9d871e/9d8743/4985dc/49865c;
    // that capture returned 70 for every armed recruit and is preserved as a perturbed diagnostic.
    hook(0x4985c0, {
        onEnter(args) {this.observe = inRange > 0; if (this.observe) this.out = args[0];},
        onLeave() {if (this.observe && globalThis.rsgTrail) globalThis.rsgTrail.push(['maximum', u32(this.out)]);}
    });
    hook(0x499790, {
        onEnter(args) {this.observe = inRange > 0; if (this.observe) this.slot = args[0].toUInt32();},
        onLeave(ret) {if (this.observe && globalThis.rsgTrail) globalThis.rsgTrail.push(['slot', this.slot, ret.toUInt32()]);}
    });
    const arrival = new Map();
    hook(0x16e910, {onEnter() {
        const m = this.context.ecx, key = m.toString();
        const value = u32(m.add(0x90)) + ':' + u32(m.add(0xb0));
        if (arrival.get(key) === value) return;
        arrival.set(key, value); bump('arrival-range');
        emit('arrival-range', {mover: key, radius: u32(m.add(0x90)), storedRange: u32(m.add(0xb0)), tick: sampleTick});
    }});
    hook(0x497da0, {
        onEnter(args) {this.a = this.context.ecx; this.row = {attack: this.a.toString(), release: args[0].toUInt32(),
            melee: args[1].toUInt32(), ranged: args[2].toUInt32(), special: args[3].toUInt32(), before: ints(this.a.add(0x224), 3).map(v => v | 0), caller: rel(this.returnAddress)};},
        onLeave() {bump('suppression'); emit('suppression', {...this.row, after: ints(this.a.add(0x224), 3).map(v => v | 0)});}
    });
    for (const [name, rva] of [['attach', 0x9cf680], ['detach', 0x9d5610]]) hook(rva, {onEnter(args) {
        const u = args[0];
        units.set(u.toString(), u);
        bump('roster-' + name);
        emit('roster', {name, captain: this.context.ecx.toString(), captainFlags: u32(this.context.ecx.add(0x6c)), caller: rel(this.returnAddress), ...unitWords(u)});
    }});
    const captains = new Map();
    for (const [name, rva, hasMode] of [['outer-leave', 0x9d8d50, false], ['range-departure', 0x9d8eb0, true], ['range-enter', 0x9d9020, false]]) hook(rva, {
        onEnter(args) {const c = this.context.ecx; captains.set(c.toString(), c); const packet = args[hasMode ? 1 : 0];
            bump(name); emit('membership', {name, captain: c.toString(), mode: hasMode ? args[0].toInt32() : null, counts: ints(c.add(0xb8), 6).map(v => v | 0),
                unit: packet.add(0x10).readPointer().toString(), tick: sampleTick});}
    });
    hook(0x9d87d0, {onEnter(args) {
        bump('reissue');
        emit('reissue', {captain: this.context.ecx.toString(), unit: args[0].toString(), order: args[1].toUInt32(), target: args[2].toString(), caller: rel(this.returnAddress)});
    }});
    hook(0x6926b0, {onEnter(args) {
        if (!this.returnAddress.equals(at(0x5fd930))) return;
        bump('task-range');
        emit('task-range', {unit: this.context.ecx.toString(), kind: args[0].toUInt32(), target: args[1].toString(), range: u32(args[2]), extra: args[3].toUInt32()});
    }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {recording = false; return {installed, counts};}};
