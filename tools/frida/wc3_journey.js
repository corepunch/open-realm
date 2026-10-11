// WC3 1.27.1.7085 long-journey observer: exported probe rows, PATHMETA markers and
// (optionally) raw velocity commits. Read-only entry/exit hooks; rows are batched so
// multi-minute journeys do not send one message per hooked call.
let installed = false, active = false, buffer = [];
const flush = () => {if (buffer.length) {send({event: 'batch', rows: buffer}); buffer = [];}};
const push = row => {buffer.push(row); if (buffer.length >= 4096) flush();};

function install(module) {
    if (installed || module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp ||
        pe.add(80).readU32() !== config.imageSize)
        throw new Error('Target PE differs from the hash-checked DLL');
    installed = true;
    send({event: 'module', base: base.toString(), path: module.path});
    const hook = (rva, callbacks) => Interceptor.attach(base.add(rva), callbacks);
    // SaveInteger/SaveReal natives: (hashtable, parent, child, value|value*).
    for (const [rva, kind] of [[0x211520, 'integer'], [0x2116a0, 'real']]) hook(rva, {onEnter(args) {
        if (active) push(['metadata-row', kind, args[1].toInt32(), args[2].toInt32(),
            kind === 'real' ? args[3].readU32() : args[3].toUInt32()]);
    }});
    // Preload's string-intern call receives the resolved C string.
    hook(0x231df0, {onEnter(args) {
        if (args[0].isNull()) return;
        const value = args[0].readCString();
        if (value === null || !value.startsWith('PATH')) return;
        if (value.startsWith('PATHMETA case=metadata_')) active = true;
        push([value.startsWith('PATHMETA ') ? 'metadata-marker' : 'marker', value]);
        flush();
    }});
    // Optional thiscall observations: ECX, the first stack words and EAX on return; `out`
    // also reads the dword its first argument points to after return. `group` snapshots a
    // physical group at entry: flags80 and, per member (array28, stride2c, count38), member
    // flags28, mover14 and moverD8. `peek` reads ECX-relative dwords at entry.
    for (const call of config.calls || []) hook(call.rva, {
        onEnter() {
            this.ecx = this.context.ecx; this.args = Array.from({length: call.args || 0}, (_, i) => this.context.esp.add(4 + i * 4).readU32());
            this.peek = (call.peek || []).map(off => this.ecx.add(off).readU32());
            if (call.group && active) {
                const g = this.ecx, members = g.add(0x28).readPointer();
                this.group = [g.add(0x80).readU32(), Array.from({length: g.add(0x38).readU32()}, (_, i) => {
                    const m = members.add(i * 0x2c), mover = m.add(0x14).readPointer();
                    return [m.add(0x28).readU32(), mover.toString(), mover.add(0xd8).readU32()];
                })];
            }
        },
        onLeave(ret) {
            if (!active) return;
            const row = ['call', call.rva, this.ecx.toString(), this.args, ret.toUInt32()];
            row.push(call.out ? ptr(this.args[0]).readU32() : null, this.group || null, this.peek);
            push(row);
        }
    });
    if (config.commits) {
        // 16fe20 is thiscall(speed*,heading*); the committed pose/velocity words follow it.
        const words = (p, n) => Array.from({length: n}, (_, i) => p.add(i * 4).readU32());
        hook(0x16fe20, {
            onEnter() {this.mover = this.context.ecx;},
            onLeave() {if (active) push(['velocity-commit', this.mover.toString(), words(this.mover.add(0x70), 8)]);}
        });
    }
}

Process.attachModuleObserver({onAdded: install});
rpc.exports = {finish() {flush(); active = false; return {installed};}};
