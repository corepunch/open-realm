// FORM-05.2 layout-input observer (form05_observer.js plus exact 16a5b0 entry inputs); read-only observer for WC3 1.27.1.7085 game.dll (research tool, new file).
// No game functions are called and no game memory is written. `config` is prepended by the capture script.
// Formation lifecycle hooks (all original ABIs from assembly, see the handoffs):
//  231df0 Preload marker; 16b7b0 cohort bind (after 16bdb0 copied request+100 -> group+80);
//  16c150 PathGroup_TickMovement entry snapshot (group state, members, path indices);
//  16ce10 PathGroup_RequestRoute result/outputs; 1697a0 AdvanceAndRefreshFormation; 16d990 RefreshFormationPoint;
//  16a5b0 LayoutFormation (offsets after); 16b120 CountRegroupStatus; 16d6a0 ResetMemberRoutes;
//  16c5d0 StopMembers (failed group route); 16b5c0 member commit (+16be60 target, +16fe20 final speed);
//  169b50 shared-cap eligibility (commit pass); 165f10 Path_CheckAcceleratedWaypoint nonzero results (warp);
//  05c350/05c320 mover d8.01000000 setters.
let installed = false, active = false, serial = 0, base = null;
const counts = {};
const LIMIT = config.limit || 600000;
const emit = (event, data = {}) => {
    counts[event] = (counts[event] || 0) + 1;
    if ((active || event === 'module' || event === 'marker') && counts[event] <= LIMIT) send({event, seq: ++serial, ...data});
};
const u32 = p => p.readU32();
const pair = p => [p.readU32(), p.add(4).readU32()];
const hex = v => (v >>> 0).toString(16);
const inCommit = {};
function mover(m) {
    return {mover: m.toString(), pos: pair(m.add(0x78)), vel: pair(m.add(0x80)), max: u32(m.add(0x88)), facing: u32(m.add(0x8c)),
            radius: u32(m.add(0x90)), range: u32(m.add(0xb0)), d8: u32(m.add(0xd8)), time: u32(m.add(0x70)), epoch: u32(m.add(0x74)),
            path: m.add(0xa8).readPointer().toString()};
}
function pathState(p) {
    if (p.isNull()) return null;
    return {path: p.toString(), dest: pair(p.add(0x1c)), adjusted: pair(p.add(0x24)), fineCount: u32(p.add(0x50)), fineIndex: p.add(0x74).readS32(),
            accCount: u32(p.add(0x70)), accIndex: p.add(0x78).readS32(), flags: hex(u32(p.add(0x88))), retryDelay: u32(p.add(0x94)),
            retry: u32(p.add(0x98)), fineTime: u32(p.add(0x7c)), accTime: u32(p.add(0x80))};
}
function rows(g) {
    const n = u32(g.add(0x38)), data = ptr(u32(g.add(0x28))), out = [];
    for (let i = 0; i < n && i < 16; i++) {
        const r = data.add(i * 0x2c), m = r.add(0x14).readPointer();
        out.push({row: i, identity: pair(r), offset: pair(r.add(0xc)), dest: pair(r.add(0x18)), speed: u32(r.add(0x20)),
                  heading: u32(r.add(0x24)), flags: hex(u32(r.add(0x28))), ...(m.isNull() ? {} : mover(m))});
    }
    return out;
}
function group(g) {
    return {group: g.toString(), identity: pair(g.add(0x14)), flags: hex(u32(g.add(0x80))), age: u32(g.add(0x5c)), completion: u32(g.add(0x60)),
            cooldown: u32(g.add(0x68)), unseen: u32(g.add(0x6c)), point: pair(g.add(0x54)), heading: u32(g.add(0x70)),
            shared: u32(g.add(0x7c)).toString(16), target: pair(g.add(0x40)), count: u32(g.add(0x38)), path: pathState(g.add(0x3c).readPointer())};
}
function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    base = module.base;
    const pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString()});
    const hook = (rva, cb) => Interceptor.attach(base.add(rva), cb);
    const rva = a => { const r = a.sub(base); const v = r.toUInt32(); return (r.compare(ptr(0)) >= 0 && v < config.imageSize) ? hex(v) : 'ext'; };
    const chain = ctx => Thread.backtrace(ctx, Backtracer.ACCURATE).slice(0, 10).map(rva);
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (value === null || !value.startsWith(config.prefix)) return;
        if (value.indexOf(' label=start') >= 0) active = true;
        emit('marker', {value});
        if (value.indexOf(' label=complete') >= 0) active = false;
    }});
    hook(0x16b7b0, {onEnter(args) { if (active) { this.req = this.context.ecx; this.g = args[0]; this.from = rva(this.returnAddress); this.chain = chain(this.context); } },
                    onLeave() { if (this.req) emit('cohort', {request: this.req.toString(), requestFlags: hex(u32(this.req.add(0x100))),
                        requestPoint: pair(this.req.add(0xe8)), from: this.from, chain: this.chain, ...group(this.g), members: rows(this.g)}); }});
    hook(0x16c150, {onEnter() { if (active) { const g = this.context.ecx; emit('tick', {...group(g), members: rows(g)}); } }});
    hook(0x16ce10, {onEnter(args) { if (active) { this.g = this.context.ecx; this.dest = pair(args[0]); this.ready = args[2]; this.fin = args[3]; } },
                    onLeave(ret) { if (this.g) emit('route', {group: this.g.toString(), dest: this.dest, result: ret.toUInt32(), ready: u32(this.ready),
                        final: u32(this.fin), path: pathState(this.g.add(0x3c).readPointer()), flags: hex(u32(this.g.add(0x80)))}); }});
    hook(0x1697a0, {onEnter(args) { if (active) emit('advance', {group: this.context.ecx.toString(), resetMembers: args[0].toUInt32(), resetCounters: args[1].toUInt32(),
                                                                 caller: rva(this.returnAddress), path: pathState(this.context.ecx.add(0x3c).readPointer())}); }});
    hook(0x16d990, {onEnter(args) { if (active) emit('refresh', {group: this.context.ecx.toString(), point: pair(args[0]), caller: rva(this.returnAddress)}); }});
    // Exact 16a5b0 entry inputs in the production-C fixture order (verify_wc3_pathing_motion.py exact_formation):
    // count, heading, then per member pos/vel (+78 x4), mover clock (+70 x2), owner clock ([d53a48]+(14|68 by +14 bit31)+40 x3),
    // radius (+90) and authored rank ((d8>>12)&15).
    hook(0x16a5b0, {onEnter() { if (!active) return; this.g = this.context.ecx; const g = this.g, n = u32(g.add(0x38)), data = ptr(u32(g.add(0x28)));
                        const owner = base.add(0xd53a48).readPointer(), words = [n, u32(g.add(0x70))];
                        for (let i = 0; i < n && i < 16; i++) { const a = data.add(i * 0x2c + 0x14).readPointer();
                            const clock = owner.add(((u32(a.add(0x14)) & 0x80000000) ? 0x68 : 0x14) + 0x40);
                            for (const o of [0x78, 0x7c, 0x80, 0x84, 0x70, 0x74]) words.push(u32(a.add(o)));
                            for (const o of [0, 4, 8]) words.push(u32(clock.add(o)));
                            words.push(u32(a.add(0x90)), (u32(a.add(0xd8)) >>> 12) & 15); }
                        emit('layout-input', {group: g.toString(), flags: hex(u32(g.add(0x80))), caller: rva(this.returnAddress), input: words}); },
                    onLeave() { if (this.g) emit('layout', {group: this.g.toString(), heading: u32(this.g.add(0x70)), point: pair(this.g.add(0x54)),
                        caller: rva(this.returnAddress), offsets: rows(this.g).map(r => [r.row, r.offset, r.mover])}); }});
    hook(0x16b120, {onEnter(args) { if (active) { this.g = this.context.ecx; this.a = args[0]; this.b = args[1]; } },
                    onLeave(ret) { if (this.g) emit('regroup', {group: this.g.toString(), result: ret.toInt32(), out1: u32(this.a), out2: u32(this.b),
                        caller: rva(this.returnAddress), completion: u32(this.g.add(0x60)), cooldown: u32(this.g.add(0x68))}); }});
    hook(0x16d6a0, {onEnter() { if (active) emit('reset-members', {group: this.context.ecx.toString(), caller: rva(this.returnAddress)}); }});
    hook(0x16c5d0, {onEnter() { if (active) emit('stop-members', {group: this.context.ecx.toString(), caller: rva(this.returnAddress)}); }});
    hook(0x169b50, {onEnter() { this.g = this.context.ecx; this.mine = active && rva(this.returnAddress) === '16c57d'; },
                    onLeave(ret) { if (this.mine) emit('share', {group: this.g.toString(), result: ret.toUInt32()}); }});
    hook(0x16b5c0, {onEnter(args) {
        if (!active) return;
        const g = this.context.ecx, row = args[0], m = row.add(0x14).readPointer();
        this.row = {group: g.toString(), gflags: hex(u32(g.add(0x80))), member: row.toString(), mflags: hex(u32(row.add(0x28))),
                    req: u32(row.add(0x20)), heading: u32(row.add(0x24)), point: pair(args[1]), cap: u32(args[2]), ...mover(m)};
        this.tid = this.threadId; inCommit[this.threadId] = this.row;
    }, onLeave() { if (this.row) { delete inCommit[this.tid]; emit('commit', this.row); } }});
    hook(0x16be60, {onLeave(ret) { const row = inCommit[this.threadId]; if (row && !ret.isNull())
        row.target = {mover: ret.toString(), pos: pair(ret.add(0x78)), vel: pair(ret.add(0x80)), max: u32(ret.add(0x88))}; }});
    hook(0x16fe20, {onEnter(args) { const row = inCommit[this.threadId]; if (row && rva(this.returnAddress) === '16b6ed') { row.speed = u32(args[0]); } }});
    hook(0x165f10, {onEnter(args) { if (active) { this.p = this.context.ecx; this.src = pair(args[0]); this.force = args[1].toUInt32(); this.before = pathState(this.p); } },
                    onLeave(ret) { const r = ret.toUInt32() & 0xff; if (this.p && r !== 0) emit('warp', {result: r, source: this.src, force: this.force, before: this.before,
                        after: pathState(this.p), caller: rva(this.returnAddress)}); }});
    for (const r of [0x05c350, 0x05c320]) hook(r, {onEnter(args) { this.v = args[0].toUInt32(); this.ecx = this.context.ecx; this.caller = rva(this.returnAddress); },
        onLeave() { const m = r === 0x05c350 ? this.context.edx : this.ecx; emit('exempt', {setter: hex(r), value: this.v, caller: this.caller, ...mover(m)}); }});
}
Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() { const c = Object.assign({}, counts); active = false; return {installed, counts: c}; }};
