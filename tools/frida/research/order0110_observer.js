// ORDER-01.10/01.18 read-only observer for WC3 1.27.1.7085 (game.dll). No game calls, no writes.
// Hooks: Preload marker, SaveInteger/SaveReal rows, GetUnitCurrentOrder handle->unit binding and head read,
// CAbilityAttack dispatch 49a5f0, CAbilityMove dispatch 5fda10, internal task prepend 691e60,
// user-order append 693490, admission 680320, user-head dispatch 67abe0, order factory 690fa0/690930.
// `config` is prepended by the capture script.
let installed = false, active = false, serial = 0;
const units = new Map();
const emit = (event, data = {}) => { if (active || event === 'module' || event === 'marker') send({event, seq: ++serial, ms: Date.now(), ...data}); };
const pair = p => [p.readU32(), p.add(4).readU32()];
function unitState(unit) {
    return {unit: unit.toString(), head: pair(unit.add(0x19c)), tail: pair(unit.add(0x1a8)),
            count: unit.add(0x1b4).readU32(), task: pair(unit.add(0x174)), flags5c: unit.add(0x5c).readU32()};
}
function orderWords(o) {
    if (o.isNull()) return null;
    return {order: o.toString(), identity: pair(o.add(0xc)), command: o.add(0x24).readU32(), player: o.add(0x28).readU32(),
            flags20: o.add(0x20).readU32(), point: [o.add(0x48).readU32(), o.add(0x50).readU32()],
            alternate: [o.add(0x5c).readU32(), o.add(0x64).readU32()], target: pair(o.add(0x58))};
}
const ORDER_CODES = new Set([0xd0002, 0xd0003, 0xd000f, 0xd0010, 0xd0011, 0xd0012, 0xd0016, 0xd0017, 0xd0019]);
function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    emit('module', {base: base.toString()});
    const hook = (rva, cb) => Interceptor.attach(base.add(rva), cb);
    const rel = p => p.sub(base).toUInt32();
    const known = u => units.has(u.toString());
    // Preload string intern: first stack argument is the resolved C string.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (value === null || !(value.startsWith('PATHTRACE') || value.startsWith('PATHMETA') || value.startsWith('O110') || value.startsWith('O118'))) return;
        if (value.startsWith('PATHTRACE tick=0 label=start_')) active = true;
        if (value.startsWith('PATHMETA case=')) active = true;
        emit('marker', {value});
    }});
    for (const [rva, kind] of [[0x211520, 'integer'], [0x2116a0, 'real']]) hook(rva, {onEnter(args) {
        if (active) emit('row', {kind, parent: args[1].toInt32(), child: args[2].toInt32(),
                                 word: kind === 'real' ? args[3].readU32() : args[3].toUInt32()});
    }});
    // 2039dc: after handle resolution, EAX=unit (or 0), [EBP+8]=handle.
    Interceptor.attach(base.add(0x2039dc), function () {
        const unit = this.context.eax;
        if (unit.isNull()) return;
        const handle = this.context.ebp.add(8).readU32();
        const key = unit.toString();
        const prior = units.get(key);
        if (prior !== handle) {
            units.set(key, handle);
            emit('bind', {handle, unit: key, rawcode: unit.add(0x30).readU32(), identity: pair(unit.add(0xc))});
        }
    });
    // 203a23: EAX=current user head order object read by the native.
    Interceptor.attach(base.add(0x203a23), function () {
        const o = this.context.eax;
        emit('head-read', {unit: this.context.esi.toString(), order: o.toString(), command: o.add(0x24).readU32(),
                           identity: pair(o.add(0xc))});
    });
    for (const [rva, owner] of [[0x49a5f0, 'attack'], [0x5fda10, 'move']]) hook(rva, {
        onEnter(args) {
            this.row = null;
            const ability = this.context.ecx, unit = ability.add(0x30).readPointer();
            if (unit.isNull() || !known(unit)) return;
            const ev = args[0], code = ev.add(8).readU32();
            this.ability = ability; this.unit = unit;
            this.row = {owner, unit: unit.toString(), ability: ability.toString(), code, abilityFlags: ability.add(0x20).readU32(),
                        before: unitState(unit), caller: rel(this.returnAddress)};
            const payload = ev.add(0xc).readPointer();
            if (ORDER_CODES.has(code) && !payload.isNull()) this.row.input = orderWords(payload);
            else if (!payload.isNull() && code >= 0xd0144 && code <= 0xd0198) this.row.task = {task: payload.toString(), code: payload.add(0x30).readU32(), arg: payload.add(0x34).readU32()};
            emit(owner + '-dispatch-begin', this.row);
        },
        onLeave(result) {
            if (this.row) emit(this.row.owner + '-dispatch-end', {unit: this.row.unit, code: this.row.code, result: result.toUInt32(),
                abilityFlags: this.ability.add(0x20).readU32(), after: unitState(this.unit)});
        }
    });
    hook(0x691e60, {onEnter(args) {
        const unit = this.context.ecx;
        if (!known(unit)) return;
        const t = args[0];
        emit('task-prepend', {unit: unit.toString(), task: t.toString(), identity: pair(t.add(0xc)), code: t.add(0x30).readU32(),
                              arg: t.add(0x34).readU32(), headBefore: pair(unit.add(0x174)), caller: rel(this.returnAddress)});
    }});
    hook(0x693490, {
        onEnter(args) {
            this.unit = this.context.ecx; this.row = null;
            if (!known(this.unit)) return;
            this.row = {unit: this.unit.toString(), order: orderWords(args[0]), before: unitState(this.unit), caller: rel(this.returnAddress)};
        },
        onLeave(r) { if (this.row) emit('user-append', {...this.row, after: unitState(this.unit)}); }
    });
    hook(0x680320, {
        onEnter(args) {
            this.unit = this.context.ecx; this.row = null;
            if (!known(this.unit)) return;
            this.row = {unit: this.unit.toString(), order: orderWords(args[0]), mode: args[1].toUInt32(), dispatch: args[2].toUInt32(),
                        before: unitState(this.unit), caller: rel(this.returnAddress)};
        },
        onLeave(r) { if (this.row) emit('user-admit', {...this.row, result: r.toUInt32(), after: unitState(this.unit)}); }
    });
    hook(0x67abe0, {
        onEnter(args) {
            this.unit = this.context.ecx; this.row = null;
            if (!known(this.unit)) return;
            this.row = {unit: this.unit.toString(), order: orderWords(args[0]), flag: args[1].toUInt32(), before: unitState(this.unit),
                        caller: rel(this.returnAddress)};
            emit('user-head-dispatch-begin', this.row);
        },
        onLeave() { if (this.row) emit('user-head-dispatch-end', {unit: this.row.unit, after: unitState(this.unit)}); }
    });
    // Immediate order factory 690930 (fastcall ECX=command, EDX=player) and point/continuation factory 690fa0.
    hook(0x690930, {
        onEnter() { this.row = active ? {command: this.context.ecx.toUInt32(), player: this.context.edx.toUInt32(), caller: rel(this.returnAddress)} : null; },
        onLeave(r) { if (this.row) emit('order-factory-immediate', {...this.row, order: r.toString()}); }
    });
    hook(0x690fa0, {
        onEnter(args) { this.row = active ? {command: this.context.ecx.toUInt32(), player: this.context.edx.toUInt32(),
            point: [args[1].readU32(), args[2].readU32()], alternate: [args[3].readU32(), args[4].readU32()], caller: rel(this.returnAddress)} : null; },
        onLeave(r) { if (this.row) emit('order-factory-point', {...this.row, order: r.toString()}); }
    });
}
Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() { const r = {installed, serial, units: units.size}; active = false; return r; }};
