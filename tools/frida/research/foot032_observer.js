// FOOT-03.2 read-only observer for WC3 1.27.1.7085 game.dll. No calls into game code, no writes.
// Markers: Preload string intern 231df0 ("FOOT032 ...").  Between begin-* and end-* markers it
// records the four fine consumers' calls on the two site windows: 1489a0 (fine/segment/endpoint
// cell predicate), 148e90 (hierarchy cell predicate), 148ad0 (blocker collector), 1492b0/149320
// (endpoint footprint/point wrappers) and 15d0e0 (hierarchy block classifier).  At every end-*
// marker it snapshots the window cells, their complete lazy chains, object words and hierarchy.
let installed = false, recording = true, active = null;
const counts = {};
const emit = (event, data = {}) => {if (recording) send({event, ms: Date.now(), ...data});};
const bump = k => {counts[k] = (counts[k] || 0) + 1;};
const u32 = p => p.readU32() >>> 0;
const WINDOWS = config.windows; // [[x0,y0,x1,y1] inclusive]
const inWindow = (x, y) => WINDOWS.some(w => x >= w[0] && x <= w[2] && y >= w[1] && y <= w[3]);
const LIMIT = 4000;

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const at = rva => base.add(rva);
    const hook = (rva, cb) => Interceptor.attach(at(rva), cb);
    const owner = () => at(0xd53a48).readPointer();
    const objectWords = o => {
        const row = {object: o.toString(), identity: [u32(o.add(0x14)), u32(o.add(0x18))], rect: [0, 4, 8, 12].map(i => o.add(0x1c + i).readS32()),
            map: o.add(0x2c).readPointer().toString(), w34: u32(o.add(0x34)), w38: u32(o.add(0x38)), w3c: u32(o.add(0x3c)), w40: u32(o.add(0x40))};
        const payload = o.add(0x30).readPointer();
        row.payload = payload.toString();
        if (!payload.isNull()) {
            try {row.payloadTag0c = u32(payload.add(0xc)); row.payloadTag10 = u32(payload.add(0x10));} catch (e) {row.payloadTag = 'unreadable';}
        }
        return row;
    };
    const chain = (fine, x, y) => {
        const width = fine.add(0x3c).readS32();
        const word = u32(fine.add(0x28).readPointer().add((y * width + x) * 4));
        const links = fine.add(0x78).readPointer(), records = [];
        let cursor = word & 0xffffff, guard = 0;
        while (cursor !== 0xffffff && guard++ < 256) {
            const link = links.add(cursor * 8), head = u32(link), kind = head >>> 24;
            const rec = {index: cursor, kind};
            if (kind !== 2) rec.obj = objectWords(link.add(4).readPointer());
            else rec.meta = u32(link.add(4));
            records.push(rec);
            cursor = head & 0xffffff;
        }
        return {x, y, word, high: word >>> 24, records, truncated: cursor !== 0xffffff};
    };
    const snapshot = marker => {
        const o = owner(); if (o.isNull()) return;
        const fine = o.add(0x238).readPointer(), sys = o.add(0x24c).readPointer();
        const cells = [];
        for (const w of WINDOWS) for (let y = w[1]; y <= w[3]; y++) for (let x = w[0]; x <= w[2]; x++) cells.push(chain(fine, x, y));
        const hierarchy = [];
        for (let level = 0; level < 2; level++) {
            const m = o.add(0x23c + level * 4).readPointer(), lw = m.add(0x3c).readS32(), data = m.add(0x28).readPointer();
            const s = 2 << level, rows = [];
            for (const w of WINDOWS) for (let y = w[1] >> (level + 1); y <= w[3] >> (level + 1); y++)
                for (let x = w[0] >> (level + 1); x <= w[2] >> (level + 1); x++)
                    rows.push({x, y, word0: u32(data.add((y * lw + x) * 8)), word1: u32(data.add((y * lw + x) * 8 + 4))});
            hierarchy.push({level, scale: s, rows});
        }
        emit('snapshot', {marker, map: {records_b0: u32(fine.add(0xb0)), links_88: u32(fine.add(0x88)), free_ac: u32(fine.add(0xac)),
            stamp_b4: u32(fine.add(0xb4)), width: fine.add(0x3c).readS32(), height: fine.add(0x40).readS32()},
            fine_system: {a4: u32(sys.add(0xa4)), a8: u32(sys.add(0xa8)), d4: u32(sys.add(0xd4))},
            registry: [at(0xd68610).readPointer().toString(), at(0xd6860c).readPointer().toString()], cells, hierarchy});
    };
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (!value.startsWith('FOOT032 ')) return;
        emit('marker', {value});
        const label = (value.match(/ label=([a-z-]+)/) || [])[1] || '';
        if (label.startsWith('begin-')) {active = value; bump('window');}
        if (label.startsWith('end-') || label.startsWith('probe-') || label === 'start' || label === 'complete' || label === 'site-done') snapshot(value);
        if (label.startsWith('end-')) active = null;
    }});
    const cellHook = (rva, name, read) => hook(rva, {
        onEnter(args) {
            this.row = null;
            if (!active) return;
            bump(name + '-all');
            const row = read(this, args);
            if (!row || !inWindow(row.x, row.y)) return;
            if ((counts[name] || 0) >= LIMIT) {bump(name + '-dropped'); return;}
            bump(name);
            row.window = active; row.caller = this.returnAddress.sub(base).toUInt32();
            const sys = this.context.ecx;
            if (name !== 'classify') {row.a4 = u32(sys.add(0xa4)); row.a8 = u32(sys.add(0xa8)); row.d4 = u32(sys.add(0xd4)); row.d0 = u32(sys.add(0xd0));}
            this.row = row;
        },
        onLeave(ret) {
            if (!this.row) return;
            const row = this.row;
            row.result = ret.toUInt32();
            if (row.vector) row.vectorAfter = u32(row.vector.add(0x1c)), row.vector = row.vector.toString();
            if (row.cellPtr) {row.classWord = u32(row.cellPtr.add(4)); row.cellPtr = row.cellPtr.toString();}
            emit(name, row);
        }
    });
    cellHook(0x1489a0, 'fine-cell', (c, a) => ({x: a[0].toInt32(), y: a[1].toInt32()}));
    cellHook(0x148e90, 'hier-cell', (c, a) => ({x: a[0].toInt32(), y: a[1].toInt32()}));
    cellHook(0x148ad0, 'collect-cell', (c, a) => ({x: a[0].toInt32(), y: a[1].toInt32(), vector: a[2], vectorBefore: u32(a[2].add(0x1c))}));
    cellHook(0x1492b0, 'endpoint-footprint', (c, a) => {const p = a[0]; return {x: p.readS32(), y: p.add(4).readS32(), mask: u32(a[1]), klass: a[2].toUInt32() & 0xffff};});
    cellHook(0x149320, 'endpoint-point', (c, a) => {const p = a[0]; return {x: Math.floor(p.readFloat()), y: Math.floor(p.add(4).readFloat()), fx: p.readFloat(), fy: p.add(4).readFloat(), mask: u32(a[1])};});
    cellHook(0x15d0e0, 'classify', (c, a) => ({x: a[3].toInt32(), y: a[4].toInt32(), cellPtr: a[0], mask: u32(a[1]), shift: a[2].toUInt32()}));
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {recording = false; return {installed, counts};}};
