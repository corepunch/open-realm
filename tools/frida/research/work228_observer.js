// MAP-02.2 read-only observer for WC3 1.27.1.7085 (game.dll). No calls, no writes.
// Captures: file-backed loader cells/hierarchy (04c860), full fine snapshots at probe
// markers, unit support refresh (684480) and the 66d780/78d1e0/78bd60/64eca0 inputs.
let installed = false, recording = true;
const counts = {};
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = k => {counts[k] = (counts[k] || 0) + 1;};
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readS32());
const u32 = p => p.readU32() >>> 0;
const fbits = p => p.readU32() >>> 0;
const f = p => p.readFloat();
const PROBE_TYPES = new Set(config.rawcodes);

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);

    // Work228: all storage reads are bounded by the file-backed fixture size.
    function airGrid(t) {
        const g = t.add(0x7a4), w = g.readS32(), h = g.add(4).readS32();
        if(w < 2 || h < 2 || w*h > 65536) throw new Error('unexpected flyer fixture grid');
        const p=g.add(0x18).readPointer();
        return {width:w,height:h,cell:[f(g.add(8)),f(g.add(12))],
            origin:[f(t.add(0xc8)),f(t.add(0xc4))],bits:Array.from({length:w*h},(_,i)=>fbits(p.add(i*4)))};
    }
    hook(0x7474d0,{onEnter(){this.t=this.context.ecx;},onLeave(){emit('air-initial',{grid:airGrid(this.t)});}});
    hook(0x7307b0,{onEnter(args){this.t=this.context.ecx;this.rect=Array.from({length:4},(_,i)=>f(args[0].add(i*4)));this.heightBits=args[1].toUInt32();},
        onLeave(){emit('air-raise',{rect:this.rect,heightBits:this.heightBits,grid:airGrid(this.t)});}});
    hook(0x73fc80,{onEnter(){this.t=this.context.ecx;emit('air-smooth-input',{grid:airGrid(this.t)});},
        onLeave(){emit('air-smooth-output',{grid:airGrid(this.t)});}});
    hook(0x73fce8,function(){emit('air-radius',{value:this.context.eax.toInt32()});});
    hook(0x73fd7a,function(){emit('air-levels',{value:this.context.eax.toInt32()});});
    let airSamples=0;const samples=new Map();
    hook(0x743810,{onEnter(args){
        const active=this.returnAddress.sub(base).toUInt32()===0x7434c2 && airSamples<5000;
        samples.set(Process.getCurrentThreadId(),active);
        if(active) emit('air-sample-input',{point:[f(args[1]),f(args[1].add(4))],
            caller:this.returnAddress.sub(base).toUInt32()});
    },onLeave(){samples.delete(Process.getCurrentThreadId());}});
    hook(0x743902,function(){if(!samples.get(Process.getCurrentThreadId()))return;airSamples++;
        emit('air-sample-result',{resultBits:fbits(this.context.ebp.add(12))});});
    const fineWords = () => {
        const owner = at(0xd53a48).readPointer();
        if (owner.isNull()) return null;
        const fine = owner.add(0x238).readPointer(), [width, height] = ints(fine.add(0x3c), 2);
        const data = fine.add(0x28).readPointer(), words = [];
        for (let n = 0; n < width * height; n++) words.push(data.add(n * 4).readU32() >>> 0);
        const hierarchy = [];
        for (let level = 0; level < 4; level++) {
            const map = owner.add(0x23c + level * 4).readPointer(), [w, h] = ints(map.add(0x3c), 2);
            const cells = map.add(0x28).readPointer(), values = [];
            for (let i = 0; i < w * h; i++) values.push(cells.add(i * 8 + 4).readU32() >>> 0);
            hierarchy.push({level, width: w, height: h, words: values});
        }
        // Object links reachable from each linked fine cell (bounded).
        const links = fine.add(0x78).readPointer(), linked = [];
        for (let n = 0; n < words.length; n++) {
            let cursor = words[n] & 0xffffff, guard = 0;
            if (cursor === 0xffffff) continue;
            const records = [];
            while (cursor !== 0xffffff && guard++ < 64) {
                const link = links.add(cursor * 8), head = link.readU32() >>> 0, kind = head >>> 24;
                const rec = {index: cursor, kind};
                if (kind === 0 || kind === 1) {
                    const o = link.add(4).readPointer();
                    rec.category = o.add(0x34).readU32() >>> 0; rec.live = o.add(0x38).readS32();
                    rec.flags = o.add(0x40).readU32() >>> 0; rec.rect = ints(o.add(0x1c), 4); rec.object = o.toString();
                }
                records.push(rec); cursor = head & 0xffffff;
            }
            linked.push({cell: n, records});
        }
        return {width, height, words, hierarchy, linked};
    };
    // Complete file-backed load (before object constructors), as wc3_pathfinding.js map-load-complete.
    hook(0x04c860, {
        onEnter() {this.file = this.context.edx.isNull() ? null : this.context.edx.readCString();},
        onLeave() {bump('map-load'); emit('map-load-complete', {filename: this.file, snapshot: fineWords()});}
    });
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value.startsWith('MAP022 ')) return;
        emit('marker', {value});
        if (value.includes(' label=start ') || value.includes(' label=complete')) emit('cell-snapshot', {marker: value, snapshot: fineWords()});
    }});
    // Thread-local frames: 684480 support refresh, 66d780 support getter, GetLocationZ native.
    const frames = new Map();
    const top = () => {const s = frames.get(Process.getCurrentThreadId()); return s && s.length ? s[s.length - 1] : null;};
    const push = fr => {const t = Process.getCurrentThreadId(); if (!frames.has(t)) frames.set(t, []); frames.get(t).push(fr);};
    const pop = () => {const s = frames.get(Process.getCurrentThreadId()); return s.pop();};
    const unitState = u => ({rawcode: u.add(0x30).readU32() >>> 0, flags20: u32(u.add(0x20)), flags5c: u32(u.add(0x5c)),
        move1fc: u32(u.add(0x1fc)), ground200: u.add(0x200).readS32(), fly208: f(u.add(0x208)), min20c: f(u.add(0x20c)),
        max210: f(u.add(0x210)), flags280: u32(u.add(0x280)), cache: [f(u.add(0x284)), f(u.add(0x288)), f(u.add(0x28c))]});
    const isProbeUnit = u => PROBE_TYPES.has(u.add(0x30).readU32() >>> 0);
    const last = new Map();
    hook(0x684480, {
        onEnter(args) {
            const u = this.context.ecx;
            this.fr = null;
            if (!isProbeUnit(u)) return;
            this.fr = {kind: 'refresh', unit: u, out: args[0], force: args[1].toUInt32(), before: unitState(u), calls: [],
                caller: this.returnAddress.sub(base).toUInt32()};
            push(this.fr);
        },
        onLeave() {
            if (!this.fr) return;
            pop();
            const fr = this.fr, u = fr.unit, after = unitState(u);
            const out = [f(fr.out), f(fr.out.add(4)), f(fr.out.add(8))];
            const key = u.toString(), sig = JSON.stringify([out, after.flags280, fr.calls.length ? 1 : 0, after.move1fc]);
            bump('refresh');
            if (fr.calls.length === 0 && last.get(key) === sig) return;
            last.set(key, sig);
            emit('support-refresh', {unit: key, caller: fr.caller, force: fr.force, out,
                outBits: [fbits(fr.out), fbits(fr.out.add(4)), fbits(fr.out.add(8))], before: fr.before, after, calls: fr.calls,
                getterZ: fr.getterZ, getterBridge: fr.getterBridge, deepWater: fr.deepWater});
        }
    });
    // After e4's FSTP: [EBP-0xc] = support Z returned, [EBP+0xc] = on-bridge out flag.
    hook(0x684597, function () {
        const fr = top(); if (!fr || fr.kind !== 'refresh') return;
        const ebp = this.context.ebp;
        fr.getterZ = f(ebp.sub(0xc)); fr.getterBridge = ebp.add(0xc).readU32();
    });
    hook(0x66d780, {
        onEnter(args) {
            const u = this.context.ecx; this.fr = null;
            if (!isProbeUnit(u)) return;
            const p = args[0];
            this.fr = {kind: 'getter', point: [f(p), f(p.add(4))], layer: args[1].toInt32(), outFlag: !args[2].isNull(),
                force: args[3].toUInt32(), state: unitState(u), events: [], caller: this.returnAddress.sub(base).toUInt32()};
            push(this.fr);
        },
        onLeave() {
            if (!this.fr) return; pop();
            const parent = top(), row = {...this.fr};
            if (parent && parent.kind === 'refresh') parent.calls.push(row); else {bump('getter-other'); emit('support-getter', row);}
        }
    });
    const getterProbe = (rva, name, read) => hook(rva, function () {
        const fr = top(); if (!fr || fr.kind !== 'getter') return;
        fr.events.push({at: name, ...read(this.context)});
    });
    getterProbe(0x66d7b3, 'building-path', c => ({}));
    getterProbe(0x66d862, 'cached-z', c => ({z: f(c.esi.add(0x28c))}));
    getterProbe(0x66d8be, 'base', c => ({base: f(c.ebp.add(8))}));
    getterProbe(0x66d914, 'flyer-blend', c => ({base: f(c.ebp.add(8)), layer3: f(c.ebp.add(0x10)), fly: f(c.ebp.add(0x14)), max: c.eax.toUInt32()}));
    getterProbe(0x66d949, 'nonflyer', c => ({base: f(c.ebp.add(8)), fly: f(c.ebp.add(0x14))}));
    getterProbe(0x66d982, 'nonflyer-final', c => ({support: f(c.ebp.add(8)), fly: f(c.ebp.add(0x14))}));
    // GetLocationZ native frame.
    hook(0x200cd0, {
        onEnter() {this.fr = {kind: 'locz', events: []}; push(this.fr);},
        onLeave(ret) {pop(); bump('locz'); const b = ret.toUInt32() >>> 0;
            const buf = Memory.alloc(4); buf.writeU32(b);
            emit('getlocationz', {resultBits: b, result: buf.readFloat(), events: this.fr.events});}
    });
    const anyFrame = () => {const fr = top(); return fr && (fr.kind === 'getter' || fr.kind === 'locz') ? fr : null;};
    hook(0x78d1e0, {onEnter(args) {
        const fr = anyFrame(); this.fr = fr; if (!fr) return;
        this.ev = {at: '78d1e0', layer: this.context.ecx.toInt32(), x: args[0].toUInt32(), y: args[1].toUInt32(), flag: args[2].toUInt32()};
        const b = Memory.alloc(8); b.writeU32(this.ev.x); b.add(4).writeU32(this.ev.y); this.ev.xy = [b.readFloat(), b.add(4).readFloat()];
        fr.events.push(this.ev); fr.terrainEvent = this.ev;
    }});
    hook(0x78d21e, function () {const fr = anyFrame(); if (fr && fr.terrainEvent) fr.terrainEvent.terrain = f(this.context.ebp.add(8));});
    hook(0x78d25a, function () {const fr = anyFrame(); if (fr && fr.terrainEvent) {fr.terrainEvent.result = f(this.context.ebp.add(8)); fr.terrainEvent.bridge = this.context.edi.toUInt32(); fr.terrainEvent.deckOrY = f(this.context.ebp.add(0xc)); fr.terrainEvent = null;}});
    hook(0x78bd60, {
        onEnter() {this.fr = anyFrame(); if (!this.fr) return; this.out = this.context.edx; const p = this.context.ecx; this.ev = {at: '78bd60', point: [f(p), f(p.add(4))]};},
        onLeave(ret) {if (!this.fr) return; this.ev.present = ret.toUInt32(); this.ev.water = this.out.isNull() ? null : f(this.out); this.fr.events.push(this.ev);}
    });
    hook(0x64eca0, {
        onEnter(args) {const fr = top(); this.fr = fr && fr.kind === 'refresh' ? fr : null; if (!this.fr) return;
            const b = Memory.alloc(8); b.writeU32(args[0].toUInt32()); b.add(4).writeU32(args[1].toUInt32()); this.xy = [b.readFloat(), b.add(4).readFloat()];},
        onLeave(ret) {if (this.fr) this.fr.deepWater = {xy: this.xy, result: ret.toUInt32()};}
    });
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {recording = false; return {installed, counts};}};
