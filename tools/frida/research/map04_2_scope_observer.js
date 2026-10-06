// MAP-04.1/MAP-04.2 read-only exclusion-scope observer, WC3 game.dll 1.27.1.7085.
// Entry/exit hooks only: no gameplay calls, no writes to target memory.
// `config` is prepended by tools/frida/research/map04_2_trace_scopes.py.
let installed = false, recording = true, serial = 0;
const counts = {}, stacks = new Map(), limits = {};
const emit = (event, data = {}) => {if (recording) send({event, seq: serial++, ms: Date.now(), ...data});};
const bump = kind => {counts[kind] = (counts[kind] || 0) + 1; return counts[kind];};
const ints = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readS32());
const hex = v => (v >>> 0).toString(16);

const SCOPES = {
    0x166c30: 'coarse_request', 0x166e90: 'fine_request', 0x167bf0: 'visible_waypoint',
    0x166140: 'next_step_blockers', 0x16ec00: 'portal_fine_point', 0x16ee80: 'separation_endpoint',
    0x170080: 'embedded_recovery', 0x16bcf0: 'group_publish', 0x059590: 'rectangle_list_query',
    0x05ca50: 'stop_with_recovery', 0x04df50: 'point_query', 0x0599c0: 'unreferenced_mixed_query',
};
const WRITERS = {
    0x15d360: 'hierarchy_rectangle', 0x054000: 'terrain_cell_flags', 0x14e770: 'spatial_rectangle',
    0x160590: 'mover_fine_bounds', 0x14d960: 'spatial_emit_rectangle', 0x04e0b0: 'hierarchy_full',
    0x05bd30: 'unit_counter_toggle', 0x063d10: 'widget_list_counter_toggle', 0x16da60: 'group_target_counter_toggle',
    0x169c50: 'group_member_acquire', 0x169d60: 'group_member_release', 0x651590: 'unit_virtual_exclusion_toggle',
};
const LIMIT = config.limit || 600;

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp ||
        pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString(), path: module.path});
    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);
    const owner = () => base.add(0xd53a48).readPointer();
    const stack = tid => {let s = stacks.get(tid); if (!s) {s = []; stacks.set(tid, s);} return s;};

    // Hierarchy class bytes of the rounded coverage of fine rectangles (+margin) at every level.
    function coverage(rects) {
        const o = owner();
        if (o.isNull()) return null;
        const out = [];
        for (let level = 0; level < 4; level++) {
            const map = o.add(0x23c + 4 * level).readPointer();
            if (map.isNull()) return null;
            const data = map.add(0x28).readPointer(), w = map.add(0x3c).readU32(), h = map.add(0x40).readU32();
            const scale = 2 << level;
            let x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
            for (const r of rects) {
                // WC3PathIntegerRectangle min_y,min_x,max_y,max_x
                const M = config.margin || 1;
                y0 = Math.min(y0, Math.floor(r[0] / scale) - M); x0 = Math.min(x0, Math.floor(r[1] / scale) - M);
                y1 = Math.max(y1, Math.floor(r[2] / scale) + M); x1 = Math.max(x1, Math.floor(r[3] / scale) + M);
            }
            x0 = Math.max(0, x0); y0 = Math.max(0, y0); x1 = Math.min(w - 1, x1); y1 = Math.min(h - 1, y1);
            const rows = [];
            for (let y = y0; y <= y1; y++) {
                let row = '';
                for (let x = x0; x <= x1; x++) row += data.add((y * w + x) * 8 + 7).readU8().toString(16).padStart(2, '0');
                rows.push(row);
            }
            out.push({level, x0, y0, rows});
        }
        return out;
    }
    // Every spatial object linked in the fine cells of a rectangle, with its +40 word.
    function linked(rect) {
        const o = owner();
        const system = o.add(0x24c).readPointer(), map = system.add(0x1c).readPointer();
        const cells = map.add(0x28).readPointer(), w = map.add(0x3c).readU32(), h = map.add(0x40).readU32();
        const links = map.add(0x78).readPointer();
        const seen = {};
        const [y0, x0, y1, x1] = rect;
        if ((y1 - y0) * (x1 - x0) > 400) return {truncated: true};
        for (let y = Math.max(0, y0); y < Math.min(h, y1); y++) for (let x = Math.max(0, x0); x < Math.min(w, x1); x++) {
            let at = cells.add((y * w + x) * 4).readU32() & 0xffffff, guard = 0;
            while (at !== 0xffffff && guard++ < 64) {
                const word = links.add(at * 8).readU32();
                if ((word & 0xff000000) !== 0x2000000) {
                    const obj = links.add(at * 8 + 4).readPointer();
                    const key = obj.toString();
                    if (!seen[key]) seen[key] = {object: key, word40: hex(obj.add(0x40).readU32()),
                        category: hex(obj.add(0x34).readU32()), rect: ints(obj.add(0x1c), 4), cells: 0};
                    seen[key].cells++;
                }
                at = word & 0xffffff;
            }
        }
        return Object.values(seen);
    }
    function objectState(p) {
        if (p.isNull()) return null;
        return {object: p.toString(), word40: hex(p.add(0x40).readU32()), rect: ints(p.add(0x1c), 4)};
    }

    for (const [rvaText, name] of Object.entries(SCOPES)) {
        const rva = Number(rvaText);
        hook(rva, {
            onEnter(args) {
                const s = stack(this.threadId);
                this.name = name;
                this.scopeDepth = s.length;
                this.outer = s.map(e => e.name);
                this.n = bump('enter-' + name);
                this.id = serial;
                const entry = {name, id: this.id};
                s.push(entry);
                this.emitRow = this.n <= LIMIT;
                this.row = {scope: name, n: this.n, id: this.id, depth: this.scopeDepth, outer: this.outer,
                    caller: hex(this.returnAddress.sub(base).toUInt32()), thread: this.threadId};
                if (name === 'embedded_recovery' && this.emitRow) {
                    const obj = this.context.ecx.add(0x98).readPointer();
                    this.recoveryObject = obj;
                    this.row.moverObject = objectState(obj);
                }
                if (name === 'coarse_request' || name === 'fine_request') {
                    const path = this.context.ecx;
                    this.path = path;
                    this.self = path.add(0xa0).readPointer();
                    this.target = path.add(0xa4).readPointer();
                    entry.self = this.self; entry.target = this.target;
                    this.detail = this.n <= LIMIT;
                    if (this.detail) {
                        const rects = [this.self, this.target].filter(p => !p.isNull()).map(p => ints(p.add(0x1c), 4));
                        Object.assign(this.row, {path: path.toString(), flags: hex(path.add(0x88).readU32()),
                            self: objectState(this.self), target: objectState(this.target),
                            source: args[0].isNull() ? null : ints(args[0], 2), goal: args[1].isNull() ? null : ints(args[1], 2)});
                        if (name === 'coarse_request') {this.rects = rects; this.row.classes = coverage(rects);}
                        if (name === 'fine_request' && !this.target.isNull()) this.row.targetLinks = linked(ints(this.target.add(0x1c), 4));
                    }
                }
                if (this.emitRow) emit('scope-enter', this.row);
            },
            onLeave(result) {
                const s = stack(this.threadId);
                const top = s.pop();
                const row = {scope: this.name, n: this.n, id: this.id, result: result.toInt32(),
                    lifo: !!top && top.id === this.id && top.name === this.name};
                if (this.detail) {
                    Object.assign(row, {self: objectState(this.self), target: objectState(this.target),
                        selfNow: hex(this.path.add(0xa0).readU32()), targetNow: hex(this.path.add(0xa4).readU32()),
                        flags: hex(this.path.add(0x88).readU32())});
                    if (this.name === 'coarse_request') row.classes = coverage(this.rects);
                    if (this.name === 'fine_request' && !this.target.isNull()) row.targetLinks = linked(ints(this.target.add(0x1c), 4));
                }
                if (this.recoveryObject) row.moverObject = objectState(this.recoveryObject);
                if (!row.lifo) emit('scope-lifo-violation', row);
                if (this.emitRow) emit('scope-leave', row);
            }
        });
    }
    // Search entries: state actually seen by the searches inside a request scope.
    hook(0x148100, {onEnter() {
        const s = stack(this.threadId), top = s[s.length - 1];
        if (!top || top.name !== 'fine_request' || bump('fine-search') > LIMIT) return;
        const arg = this.context.esp.add(28).readPointer();
        emit('fine-search', {id: top.id, targetArgument: arg.toString(),
            self: objectState(top.self), target: objectState(top.target),
            targetLinks: top.target.isNull() ? null : linked(ints(top.target.add(0x1c), 4))});
    }});
    hook(0x162cb0, {onEnter() {
        const s = stack(this.threadId), top = s[s.length - 1];
        if (!top || top.name !== 'coarse_request' || bump('coarse-search') > LIMIT) return;
        const rects = [top.self, top.target].filter(p => !p.isNull()).map(p => ints(p.add(0x1c), 4));
        emit('coarse-search', {id: top.id, self: objectState(top.self), target: objectState(top.target),
            classes: rects.length ? coverage(rects) : null});
    }});
    for (const [rvaText, name] of Object.entries(WRITERS)) {
        const rva = Number(rvaText);
        hook(rva, {
            onEnter(args) {
                const s = stack(this.threadId);
                const n = bump('writer-' + name);
                const inside = s.length > 0;
                this.observe = (inside || name === 'hierarchy_rectangle' || name === 'terrain_cell_flags' ||
                    name === 'hierarchy_full' || name === 'unit_virtual_exclusion_toggle') && n <= 4 * LIMIT;
                if (!this.observe) return;
                const row = {writer: name, n, scopes: s.map(e => e.name + '#' + e.id),
                    caller: hex(this.returnAddress.sub(base).toUInt32())};
                if (name === 'hierarchy_rectangle') {
                    row.rect = args[0].isNull() ? null : ints(args[0], 4);
                    row.mode = args[1].toInt32();
                    row.objectWord40 = args[0].isNull() ? null : hex(args[0].sub(0x1c).add(0x40).readU32());
                    if (row.rect && inside) {this.rect = row.rect; row.classes = coverage([row.rect]);}
                }
                if (name === 'terrain_cell_flags') {
                    row.cell = this.context.ecx.toString();
                    row.word = this.context.ecx.isNull() ? null : hex(this.context.ecx.readU32());
                    row.flags = this.context.edx.toUInt32() & 0xff; row.set = args[0].toInt32();
                }
                if (name === 'unit_counter_toggle' || name === 'group_target_counter_toggle' ||
                    name === 'widget_list_counter_toggle' || name === 'unit_virtual_exclusion_toggle') row.on = args[0].toInt32();
                if (name === 'unit_virtual_exclusion_toggle') row.unit = this.context.ecx.toString();
                this.counterResult = name === 'unit_counter_toggle' || name === 'group_target_counter_toggle';
                this.row = row;
                emit('writer-enter', row);
            },
            onLeave(result) {
                if (!this.observe) return;
                const row = {writer: this.row.writer, n: this.row.n};
                // 05bd30/16da60 leave EAX = the spatial object whose +40 was just changed (inc/dec [eax+40]).
                if (this.counterResult && !result.isNull()) row.object = objectState(result);
                if (this.rect) row.classes = coverage([this.rect]);
                emit('writer-leave', row);
            }
        });
    }
    // Preload string intern (same anchor as tools/frida/wc3_pathfinding.js): JASS markers.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (/^PATH[A-Z]+ /.test(value)) emit('marker', {value});
    }});
    hook(0x2148f0, {onEnter(args) {
        emit('terrain-native', {x: args[0].readFloat(), y: args[1].readFloat(), pathingType: args[2].toInt32(),
            passable: args[3].toInt32(), scopes: stack(this.threadId).map(e => e.name)});
    }});
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {status() {return {installed, counts};}, finish() {recording = false; return {installed, counts};}};
