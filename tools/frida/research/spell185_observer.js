// Append to target021_observer.js. Read-only producer/clock hooks; no game calls or writes.
Process.attachModuleObserver({onAdded(module) {
    if (module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Spell185 PE mismatch');
    const words = (p, n) => Array.from({length: n}, (_, i) => u32(p.add(4 * i)));
    const stamp = () => {
        const owner = base.add(0xd53a48).readPointer();
        return {c: u32(owner.add(0x538)), clock: words(owner.add(0x54), 3)};
    };
    const row = (event, data) => {if (active) {bump(event); emit(event, {...stamp(), ...data});}};
    Interceptor.attach(base.add(0x438680), {onEnter() {
        if (!active) return;
        const a = this.context.ecx;
        row('spell-approach', {ability: a.toString(), caster: a.add(0x30).readPointer().toString(), rank: a.add(0x50).readS32()});
    }});
    Interceptor.attach(base.add(0x417f90), {onEnter(args) {
        this.out = active ? args[0] : null;
        this.buffer = args[2].toUInt32();
    }, onLeave() {if (this.out) row('spell-range', {range: u32(this.out), buffer: this.buffer});}});
    Interceptor.attach(base.add(0x41d1f0), {onEnter(args) {
        this.data = active ? {caster: this.context.ecx.toString(), range: u32(args[0]), target: args[1].toString()} : null;
    }, onLeave(ret) {if (this.data) row('spell-range-result', {...this.data, accepted: ret.toUInt32()});}});
    Interceptor.attach(base.add(0x16c150), {onEnter() {
        this.record = active;
        if (this.record) row('spell-owner-clock', {g: this.context.ecx.toString(), phase: 'begin'});
    }, onLeave() {if (this.record) row('spell-owner-clock', {phase: 'end'});}});
}});
