// SEP-03.x / MAP-05.x / MAP-06.x read-only observer for WC3 1.27.1.7085 game.dll.
// Entry/exit hooks only: reads memory, never calls game code, never writes target data.
// `config` is prepended by sep03_map06_trace.py.
let installed = false, recording = true, base = null;
const counts = {}, caps = {};
const emit = (event, data = {}) => {
    if (!recording) return;
    counts[event] = (counts[event] || 0) + 1;
    const cap = config.caps[event] ?? config.caps.default;
    if (counts[event] > cap) { caps[event] = (caps[event] || 0) + 1; return; }
    send({event, ms: Date.now(), ...data});
};
const hex = v => (v >>> 0).toString(16).padStart(8, '0');
const u32 = p => p.readU32() >>> 0;
const s32 = p => p.readS32();
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(4 * i).readS32());
const words = (p, n) => Array.from({length: n}, (_, i) => hex(p.add(4 * i).readU32()));
let currentName = null, scenarioTick = 0, loadGeneration = 0;
const objNames = new Map(), moverNames = new Map(), trackedMovers = new Set();
let metadataSinceCompaction = {};

function owner() { return base.add(0xd53a48).readPointer(); }
function clockTime() {
    const o = owner();
    return o.isNull() ? null : hex(o.add(0x1a4).readU32());
}
function mapKind(map) {
    const o = owner();
    if (o.isNull() || map.isNull()) return 'none';
    if (o.add(0x234).readPointer().equals(map)) return 'proximity';
    if (o.add(0x238).readPointer().equals(map)) return 'fine';
    for (let i = 0; i < 4; i++) if (o.add(0x23c + 4 * i).readPointer().equals(map)) return 'adaptive' + i;
    return 'other';
}
function dirtyCount(map) {
    const n = u32(map.add(0xa8)), data = map.add(0x98).readPointer();
    let c = 0;
    for (let i = 0; i < n; i++) { let w = data.add(4 * i).readU32(); while (w) { c += w & 1; w >>>= 1; } }
    return c;
}
function mapState(map) {
    return {map: map.toString(), kind: mapKind(map), dims: ints(map.add(0x3c), 2), cells: u32(map.add(0x38)),
        cellData: map.add(0x28).readPointer().toString(), linkData: map.add(0x78).readPointer().toString(),
        growth: u32(map.add(0x80)), capacity: u32(map.add(0x84)), linkCount: u32(map.add(0x88)),
        freeHead: hex(u32(map.add(0xac))), records: u32(map.add(0xb0)), stamp: hex(u32(map.add(0xb4))),
        timer: map.add(0xb8).readPointer().toString()};
}
function objState(obj) {
    return {obj: obj.toString(), name: objNames.get(obj.toString()) || null, map: obj.add(0x2c).readPointer().toString(),
        mover: obj.add(0x30).readPointer().toString(), rect: ints(obj.add(0x1c), 4), category: hex(u32(obj.add(0x34))),
        stamp: hex(u32(obj.add(0x38))), refs: u32(obj.add(0x3c)), flags: hex(u32(obj.add(0x40))),
        id: [s32(obj.add(0x14)), s32(obj.add(0x18))]};
}
function chain(map, cell) {
    const cells = map.add(0x28).readPointer(), links = map.add(0x78).readPointer();
    let at = u32(cells.add(4 * cell)) & 0xffffff;
    const out = [];
    while (at !== 0xffffff && out.length < 256) {
        const w = u32(links.add(8 * at)), kind = w >>> 24, payload = links.add(8 * at + 4).readPointer();
        const row = [at, kind];
        if (kind === 2) row.push(hex(payload.toUInt32()));
        else row.push(payload.toString(), objNames.get(payload.toString()) || null, hex(u32(payload.add(0x38))));
        out.push(row);
        at = w & 0xffffff;
    }
    return {cell, high: hex(u32(cells.add(4 * cell)) >>> 24), chain: out, truncated: at !== 0xffffff};
}
function snapshot(label) {
    try { snapshotInner(label); } catch (e) { emit('snapshot-error', {label, error: String(e), tick: scenarioTick}); }
}
function snapshotInner(label) {
    const o = owner();
    if (o.isNull()) { emit('snapshot', {label, owner: null}); return; }
    const prox = o.add(0x234).readPointer(), fine = o.add(0x238).readPointer();
    const result = {label, tick: scenarioTick, owner: o.toString(), clock: clockTime()};
    if (!prox.isNull()) {
        const n = u32(prox.add(0x38)), cells = prox.add(0x28).readPointer(), chains = [];
        for (let c = 0; c < n; c++) if ((u32(cells.add(4 * c)) & 0xffffff) !== 0xffffff) chains.push(chain(prox, c));
        result.proximity = {state: mapState(prox), chains};
    }
    if (!fine.isNull()) {
        const w = s32(fine.add(0x3c)), seen = new Set(), chains = [];
        for (const [key, name] of objNames) {
            const obj = ptr(key);
            if (!obj.add(0x2c).readPointer().equals(fine)) continue;
            const [y0, x0, y1, x1] = ints(obj.add(0x1c), 4);
            for (let y = Math.max(0, y0); y < y1 && y < y0 + 8; y++) for (let x = Math.max(0, x0); x < x1 && x < x0 + 8; x++) {
                const c = y * w + x;
                if (!seen.has(c)) { seen.add(c); chains.push(chain(fine, c)); }
            }
        }
        result.fine = {state: mapState(fine), chains};
    }
    result.objects = Array.from(objNames.keys()).map(k => { try { return objState(ptr(k)); } catch (e) { return {obj: k, error: String(e)}; } });
    const pool = o.add(0x5d8);
    result.pool = {elementSize: u32(pool), perBlock: u32(pool.add(4)), raw: u32(pool.add(8)), live: u32(pool.add(0x18)), created: u32(pool.add(0x1c)),
        recycledHead: pool.add(0x14).readPointer().toString(), rawFree: pool.add(0x10).readPointer().toString()};
    emit('snapshot', result);
}

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    base = module.base;
    const pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);
    const caller = ctx => ctx.returnAddress.sub(base).toUInt32().toString(16);

    // JASS markers (Preload string intern; same site as wc3_pathfinding.js).
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        let value;
        try { value = args[0].readCString(); } catch (e) { return; }
        if (!value || !(value.startsWith('RSPATIAL ') || value.startsWith('RSCROWD '))) return;
        const tick = /tick=(\d+)/.exec(value);
        if (tick) scenarioTick = parseInt(tick[1]);
        emit('marker', {value, loadGeneration});
        const name = / name=([A-Za-z0-9]+)/.exec(value);
        if (value.includes('label=create_begin') && name) currentName = name[1];
        if (value.includes('label=create_end')) currentName = null;
        const label = /label=([a-z_]+)/.exec(value);
        if (label && config.snapshotLabels.includes(label[1])) snapshot(label[1]);
        if (label && label[1] === 'sample' && config.snapshotEvery > 0 && scenarioTick % config.snapshotEvery === 0) snapshot('periodic');
    }});

    // ---- spatial objects ------------------------------------------------------------
    hook(0x14cf20, {onEnter(args) { this.map = this.context.ecx; this.mover = args[0]; },
        onLeave(ret) {
            const kind = mapKind(this.map);
            if (currentName) {
                const n = currentName + (kind === 'proximity' ? '.prox' : kind === 'fine' ? '.fine' : '.' + kind);
                objNames.set(ret.toString(), n);
                moverNames.set(this.mover.toString(), currentName);
                trackedMovers.add(this.mover.toString());
            }
            emit('sobj-create', {...objState(ret), kind, caller: caller(this), tick: scenarioTick});
        }});
    hook(0x14e770, {onEnter(args) {
        const obj = this.context.ecx, next = ints(args[0], 4), old = ints(obj.add(0x1c), 4);
        this.skip = next.every((v, i) => v === old[i]);
        if (this.skip) return;
        this.obj = obj; this.next = next; this.old = old;
        this.map = obj.add(0x2c).readPointer();
        this.before = {records: u32(this.map.add(0xb0)), linkCount: u32(this.map.add(0x88)), capacity: u32(this.map.add(0x84))};
    }, onLeave() {
        if (this.skip) return;
        const name = objNames.get(this.obj.toString()) || null;
        if (!name && !config.allUpdates) return;
        emit('sobj-update', {obj: this.obj.toString(), name, kind: mapKind(this.map), old: this.old, next: this.next,
            refs: u32(this.obj.add(0x3c)), before: this.before,
            after: {records: u32(this.map.add(0xb0)), linkCount: u32(this.map.add(0x88)), capacity: u32(this.map.add(0x84))},
            tick: scenarioTick, caller: caller(this)});
    }});
    hook(0x14dae0, {onEnter() {
        const obj = this.context.ecx;
        emit('sobj-retire', {...objState(obj), kind: mapKind(obj.add(0x2c).readPointer()), tick: scenarioTick, caller: caller(this)});
    }});
    hook(0x14d750, {onEnter() {
        const obj = this.context.ecx;
        emit('sobj-recycle', {obj: obj.toString(), name: objNames.get(obj.toString()) || null, refs: hex(u32(obj.add(0x3c))),
            caller: caller(this), tick: scenarioTick});
        objNames.delete(obj.toString());   // pooled memory may be reused by a later object
    }});
    hook(0x14c880, {onEnter() {
        this.pool = this.context.ecx; this.recycled = this.pool.add(0x14).readPointer();
        this.rawFree = this.pool.add(0x10).readPointer();
    }, onLeave(ret) {
        const o = owner();
        if (o.isNull() || !this.pool.equals(o.add(0x5d8))) return;
        emit('spool-alloc', {element: ret.toString(), fromRecycled: !this.recycled.isNull(), rawFreeEmpty: this.rawFree.isNull(),
            live: u32(this.pool.add(0x18)), raw: u32(this.pool.add(8)), tick: scenarioTick});
    }});
    hook(0x06a320, {onEnter() {
        const o = owner();
        this.watch = !o.isNull() && this.context.ecx.equals(o.add(0x5d8)) && this.context.ecx.add(0x10).readPointer().isNull();
        this.pool = this.context.ecx;
    }, onLeave(ret) {
        if (this.watch) emit('spool-block', {block: this.pool.add(0xc).readPointer().toString(), element: ret.toString(),
            raw: u32(this.pool.add(8)), tick: scenarioTick, caller: caller(this)});
    }});
    // ---- link growth / compaction / metadata ---------------------------------------
    hook(0x1c5130, {onEnter(args) {
        const ecx = this.context.ecx, map = ecx.sub(0x6c), kind = mapKind(map);
        this.watch = kind === 'proximity' || kind === 'fine';
        if (!this.watch) return;
        this.map = map; this.kind = kind; this.bytes = args[0].toUInt32();
        this.before = {capacity: u32(map.add(0x84)), linkCount: u32(map.add(0x88)), data: map.add(0x78).readPointer().toString()};
        this.where = caller(this);
    }, onLeave() {
        if (!this.watch) return;
        emit('link-reserve', {kind: this.kind, bytes: this.bytes, before: this.before, caller: this.where,
            after: {capacity: u32(this.map.add(0x84)), linkCount: u32(this.map.add(0x88)), data: this.map.add(0x78).readPointer().toString()},
            backtrace: Thread.backtrace(this.context, Backtracer.ACCURATE).slice(0, 8).map(a => a.sub(base).toUInt32().toString(16)),
            tick: scenarioTick});
    }});
    hook(0x14d890, {onEnter() {
        const k = mapKind(this.context.ecx);
        metadataSinceCompaction[k] = (metadataSinceCompaction[k] || 0) + 1;
    }});
    for (const [rva, name] of [[0x14df20, 'compact-dirty'], [0x14dfc0, 'compact-all']]) hook(rva, {onEnter() {
        this.map = this.context.ecx; this.kind = mapKind(this.map);
        this.before = {...mapState(this.map), dirty: dirtyCount(this.map)};
        this.where = caller(this);
        this.meta = metadataSinceCompaction[this.kind] || 0; metadataSinceCompaction[this.kind] = 0;
        if (this.before.dirty > 0 || name === 'compact-all') emit(name + '-begin', {kind: this.kind, clock: clockTime(), tick: scenarioTick});
    }, onLeave() {
        emit(name, {kind: this.kind, clock: clockTime(), caller: this.where, metadataLinksSinceLast: this.meta,
            before: this.before, after: {records: u32(this.map.add(0xb0)), linkCount: u32(this.map.add(0x88)),
                capacity: u32(this.map.add(0x84)), freeHead: hex(u32(this.map.add(0xac))), stamp: hex(u32(this.map.add(0xb4))),
                dirty: dirtyCount(this.map)}, tick: scenarioTick});
    }});
    // ---- movers -------------------------------------------------------------------------
    hook(0x15ed40, {onEnter() { this.mover = this.context.ecx; }, onLeave() {
        const m = this.mover;
        if (currentName) { moverNames.set(m.toString(), currentName); trackedMovers.add(m.toString()); }
        emit('mover-activate', {mover: m.toString(), name: moverNames.get(m.toString()) || null,
            prox: m.add(0x94).readPointer().toString(), fine: m.add(0x98).readPointer().toString(),
            pos: words(m.add(0x78), 4), tick: scenarioTick, loadGeneration});
    }});
    hook(0x15ee60, {onEnter() {
        const m = this.context.ecx;
        emit('mover-retire', {mover: m.toString(), name: moverNames.get(m.toString()) || null,
            prox: m.add(0x94).readPointer().toString(), fine: m.add(0x98).readPointer().toString(), tick: scenarioTick, caller: caller(this)});
    }});
    hook(0x1603d0, {onEnter() { this.mover = this.context.ecx; }, onLeave() {
        const m = this.mover, key = m.toString();
        if (!config.allMovers && !trackedMovers.has(key)) return;
        emit('mover-commit', {mover: key, name: moverNames.get(key) || null, pos: words(m.add(0x78), 2), vel: words(m.add(0x80), 2),
            tick: scenarioTick, loadGeneration});
    }});
    // ---- map / owner lifetime ---------------------------------------------------------
    const ownerMaps = o => [0x234, 0x238, 0x23c, 0x240, 0x244, 0x248, 0x24c, 0x250].map(off => o.add(off).readPointer().toString());
    hook(0x0509d0, {onEnter() { this.old = owner().toString(); }, onLeave() {
        emit('owner-create', {old: this.old, owner: owner().toString(), tick: scenarioTick});
    }});
    hook(0x1591e0, {onEnter() {
        const o = this.context.ecx;
        emit('owner-destroy', {owner: o.toString(), maps: ownerMaps(o), spatialPool: {live: u32(o.add(0x5d8 + 0x18)), raw: u32(o.add(0x5d8 + 8))},
            tick: scenarioTick});
    }});
    hook(0x15ab60, {onEnter() { this.o = this.context.ecx; this.before = ownerMaps(this.o); }, onLeave() {
        const o = this.o, maps = [0x234, 0x238].map(off => mapState(o.add(off).readPointer()));
        emit('maps-create', {owner: o.toString(), before: this.before, after: ownerMaps(o), spatial: maps,
            mapPool: {live: u32(o.add(0x598 + 0x18)), created: u32(o.add(0x598 + 0x1c)), recycledHead: o.add(0x598 + 0x14).readPointer().toString()},
            tick: scenarioTick});
    }});
    hook(0x15b670, {onEnter() {
        const o = this.context.ecx; this.o = o;
        const states = [0x234, 0x238].map(off => { const m = o.add(off).readPointer(); return m.isNull() ? null : mapState(m); });
        emit('maps-release-begin', {owner: o.toString(), maps: ownerMaps(o), spatial: states,
            spatialPool: {live: u32(o.add(0x5d8 + 0x18))}, tick: scenarioTick, caller: caller(this)});
    }, onLeave() {
        emit('maps-release-end', {owner: this.o.toString(), maps: ownerMaps(this.o), spatialPool: {live: u32(this.o.add(0x5d8 + 0x18))},
            mapPool: {live: u32(this.o.add(0x598 + 0x18)), recycledHead: this.o.add(0x598 + 0x14).readPointer().toString()}});
    }});
    hook(0x14cac0, {onEnter() {
        const m = this.context.ecx; this.m = m;
        this.before = mapState(m);
    }, onLeave() {
        emit('spatial-map-release', {before: this.before, after: {records: u32(this.m.add(0xb0)), linkCount: u32(this.m.add(0x88)),
            capacity: u32(this.m.add(0x84)), freeHead: hex(u32(this.m.add(0xac))), timer: this.m.add(0xb8).readPointer().toString()},
            tick: scenarioTick});
    }});
    hook(0x04c860, {onLeave() { snapshot('maps-loaded'); }});
    // ---- save / load ----------------------------------------------------------------
    for (const [rva, name] of [[0x15b4f0, 'owner-save'], [0x15b1c0, 'owner-load'], [0x15c750, 'maps-save']]) hook(rva, {
        onEnter() { emit(name + '-begin', {owner: owner().toString(), self: this.context.ecx.toString(), tick: scenarioTick}); },
        onLeave() {
            if (name === 'owner-load') loadGeneration++;
            emit(name + '-end', {owner: owner().toString(), tick: scenarioTick, loadGeneration});
            if (name === 'owner-load') snapshot('after-owner-load');
            if (name === 'owner-save') snapshot('after-owner-save');
        }});
    hook(0x14d620, {onEnter() { this.m = this.context.ecx; this.before = mapState(this.m); },
        onLeave() { emit('map-save', {before: this.before, after: mapState(this.m)}); }});
    hook(0x14d1d0, {onEnter() { this.m = this.context.ecx; this.before = mapState(this.m); },
        onLeave() { emit('map-load', {before: this.before, after: mapState(this.m), tick: scenarioTick}); }});
    hook(0x14d430, {onEnter() {
        const obj = this.context.ecx;
        emit('sobj-save', {...objState(obj), kind: mapKind(obj.add(0x2c).readPointer())});
    }});
    hook(0x14d000, {onEnter() { this.obj = this.context.ecx; }, onLeave() {
        const obj = this.obj, m = obj.add(0x2c).readPointer();
        emit('sobj-load', {...objState(obj), kind: mapKind(m), mapRecords: m.isNull() ? null : u32(m.add(0xb0)), loadGeneration});
    }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {
    status() { return {installed, counts, caps}; },
    finish() { recording = false; return {installed, counts, caps, objNames: Object.fromEntries(objNames), moverNames: Object.fromEntries(moverNames)}; }
};
