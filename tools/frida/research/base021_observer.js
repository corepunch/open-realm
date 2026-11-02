// BASE-02.1 read-only observer for WC3 1.27.1.7085 game.dll. Entry/exit hooks only;
// no game function calls and no target writes. `config` is prepended by the controller.
let installed = false, recording = true;
const counts = {}, units = new Map();
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = k => (counts[k] = (counts[k] || 0) + 1);
const hex = v => (v >>> 0).toString(16);
function cbytes(p) {
    if (p.isNull()) return null;
    const out = [];
    for (let i = 0; i < 64; i++) {const b = p.add(i).readU8(); if (!b) break; out.push(b.toString(16).padStart(2, '0'));}
    return out.join('');
}
function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString()});
    const cunitVt = base.add(0xb77eb0);
    const rva = p => p.sub(base).toUInt32();
    const hook = (r, cb) => Interceptor.attach(base.add(r), cb);
    const isUnit = p => {try {return !p.isNull() && p.readPointer().equals(cunitVt);} catch (e) {return false;}};
    const snap = u => {
        try {
            return {unit: u.toString(), rawcode: hex(u.add(0x30).readU32()), flags20: hex(u.add(0x20).readU32()),
                flags5c: hex(u.add(0x5c).readU32()), move1fc: hex(u.add(0x1fc).readU32()), ground200: u.add(0x200).readS32(),
                fly208: u.add(0x208).readFloat(), floor20c: u.add(0x20c).readFloat(), max210: u.add(0x210).readFloat(),
                targ24c: hex(u.add(0x24c).readU32()), support280: hex(u.add(0x280).readU32()),
                cached: [u.add(0x284).readFloat(), u.add(0x288).readFloat(), u.add(0x28c).readFloat()]};
        } catch (e) {return {unit: u.toString(), error: String(e)};}
    };
    const remember = u => {if (isUnit(u) && !units.has(u.toString())) {units.set(u.toString(), u); emit('unit-seen', snap(u));}};
    // Authored parser and type tables (fastcall; ECX/EDX in, EAX out).
    hook(0x685340, {onEnter() {this.s = cbytes(this.context.ecx); this.c = rva(this.returnAddress);},
        onLeave(r) {bump('parse'); if (counts.parse <= config.samples) emit('movetp-parse', {input: this.s, bits: r.toUInt32(), caller: hex(this.c)});}});
    hook(0x685e30, {onEnter() {this.a = [this.context.ecx.toUInt32(), this.context.edx.toUInt32()]; this.c = rva(this.returnAddress);},
        onLeave(r) {bump('map'); if (counts.map <= config.samples) emit('type-map', {bits: this.a[0], edx: this.a[1], value: r.toUInt32(), caller: hex(this.c)});}});
    hook(0x685db0, {onEnter() {this.a = this.context.ecx.toUInt32(); this.c = rva(this.returnAddress);},
        onLeave(r) {bump('class'); if (counts['class'] <= config.samples) emit('type-class', {bits: this.a, flags: r.toUInt32(), caller: hex(this.c)});}});
    // Bridge publications. Unit bridge is CUnit+164 (verified by vtable word).
    const bridgeUnit = b => {const u = b.sub(0x164); return isUnit(u) ? u : null;};
    for (const [r, name, nargs] of [[0x05c7e0, 'publish-profile', 2], [0x05c6f0, 'publish-class', 1], [0x05c7b0, 'set-category', 1],
                                     [0x05c770, 'set-query', 1], [0x0594a0, 'set-adaptive', 1]]) {
        hook(r, {onEnter(args) {
            const b = this.context.ecx, u = bridgeUnit(b);
            if (u) remember(u);
            bump(name);
            if (counts[name] > config.samples) return;
            const a = []; for (let i = 0; i < nargs; i++) a.push(args[i].toUInt32());
            emit(name, {bridge: b.toString(), unit: u ? u.toString() : null, rawcode: u ? hex(u.add(0x30).readU32()) : null,
                args: a, caller: hex(rva(this.returnAddress))});
        }});
    }
    // Unit-level runtime producers.
    const unitHook = (r, name, getUnit, extra) => hook(r, {
        onEnter(args) {this.u = getUnit(this.context, args); if (!this.u || !isUnit(this.u)) {this.u = null;}
            this.row = {name, ecx: this.context.ecx.toString(), caller: hex(rva(this.returnAddress)), ...(extra ? extra(this.context, args) : {})};
            if (this.u) {remember(this.u); this.row.before = snap(this.u);} bump(name);},
        onLeave(ret) {if (counts[name] > config.samples) return; if (this.u) this.row.after = snap(this.u); this.row.ret = ret.toUInt32(); emit('producer', this.row);}
    });
    const abilityUnit = (c) => {try {return c.ecx.add(0x30).readPointer();} catch (e) {return null;}};
    unitHook(0x670950, 'rebind', c => c.ecx, (c, a) => ({newRawcode: hex(a[0].toUInt32())}));
    unitHook(0x569b80, 'morph-land', abilityUnit, c => ({abilityVt: hex(rva(c.ecx.readPointer()))}));
    unitHook(0x56a000, 'morph-takeoff', abilityUnit, c => ({abilityVt: hex(rva(c.ecx.readPointer()))}));
    unitHook(0x544c60, 'morph-dispatch', abilityUnit, c => ({abilityVt: hex(rva(c.ecx.readPointer())), normal: hex(c.ecx.add(0x18c).readU32()), alt: hex(c.ecx.add(0x190).readU32())}));
    unitHook(0x56bc30, 'morph-forms', abilityUnit, (c, a) => ({abilityVt: hex(rva(c.ecx.readPointer())), normal: hex(a[0].toUInt32()), alt: hex(a[1].toUInt32())}));
    unitHook(0x6877b0, 'ground-push', c => c.ecx);
    unitHook(0x69c840, 'ground-pop', c => c.ecx);
    unitHook(0x4ba8d0, 'burrow-category', c => c.ecx, (c, a) => ({edx: c.edx.toUInt32(), stack: a[0].toUInt32()}));
    unitHook(0x4ba940, 'submerge-category', c => c.ecx, (c, a) => ({edx: c.edx.toUInt32(), stack: a[0].toUInt32()}));
    unitHook(0x698680, 'height-set', c => c.ecx, (c, a) => ({height: a[1].readFloat(), rate: (() => {try {return a[2].readFloat();} catch (e) {return null;}})(), raise: a[3].toUInt32()}));
    unitHook(0x698630, 'floor-set', c => c.ecx, (c, a) => ({floor: a[0].readFloat()}));
    hook(0x2152c0, {onEnter(args) {emit('native-set-fly-height', {handle: args[0].toUInt32(), height: args[1].readFloat(), rate: args[2].readFloat()});}});
    // Support refresh (thiscall ECX unit, stack out xyz*, force). Emit only changed outputs.
    const lastZ = new Map();
    hook(0x684480, {onEnter(args) {this.u = this.context.ecx; this.out = args[0]; this.force = args[1].toUInt32();},
        onLeave() {
            const k = this.u.toString(); const xyz = [this.out.readFloat(), this.out.add(4).readFloat(), this.out.add(8).readFloat()];
            const key = xyz.join(',') + '|' + hex(this.u.add(0x280).readU32());
            bump('support'); if (lastZ.get(k) === key || counts['support-emit'] >= config.samples) return;
            lastZ.set(k, key); bump('support-emit');
            emit('support', {unit: k, rawcode: hex(this.u.add(0x30).readU32()), move1fc: hex(this.u.add(0x1fc).readU32()), force: this.force,
                xyz, support280: hex(this.u.add(0x280).readU32()), ground200: this.u.add(0x200).readS32(), flags5c: hex(this.u.add(0x5c).readU32())});
        }});
    // Preload string intern: markers, plus a snapshot of every remembered unit.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (value.startsWith('PATHTRACE ')) {
            emit('marker', {value});
            if (!value.includes('label=sample ') || /tick=(1|12|35|55|65|80|110|150|200|290) /.test(value))
                emit('unit-snapshots', {marker: value, units: Array.from(units.values()).map(snap)});
        } else if (value.startsWith('PATHSTOCK ')) emit('stock-marker', {value});
    }});
}
Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() {recording = false; return {installed, counts, units: units.size};}};
