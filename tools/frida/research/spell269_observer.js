// Append to target021_observer.js. Read-only producer/clock hooks; no game calls or writes.
Process.attachModuleObserver({onAdded(module) {
    if (module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Spell269 PE mismatch');
    const words = (p, n) => Array.from({length: n}, (_, i) => u32(p.add(4 * i)));
    const stamp = () => {
        const owner = base.add(0xd53a48).readPointer();
        return {c: u32(owner.add(0x538)), clock: words(owner.add(0x54), 3)};
    };
    const row = (event, data) => {if (active) {bump(event); emit(event, {...stamp(), ...data});}};
    Interceptor.attach(base.add(0x40a7a0), {onEnter(args) {
        if (!active) return;
        const a=this.context.ecx;
        row('spell-initial', {ability:a.toString(),caster:a.add(0x30).readPointer().toString(),target:args[0].toString(),widget:args[1].toString(),point:[u32(args[2]),u32(args[3])]});
    }});
    Interceptor.attach(base.add(0x416cc0), {onEnter(args) {
        this.out=active ? args[0] : null;
        if(this.out)this.data={point:[u32(args[1]),u32(args[2])],buffer:args[3].toUInt32()};
    },onLeave() {if(this.out)row('spell-point-range',{...this.data,range:u32(this.out)});}});
    Interceptor.attach(base.add(0x692120), {onEnter(args) {
        if(active)row('spell-point-task',{unit:this.context.ecx.toString(),code:args[0].toUInt32(),point:[u32(args[1]),u32(args[2])],range:u32(args[3])});
    }});
    Interceptor.attach(base.add(0x05b970), {onEnter(args) {
        if(active)row('begin-point',{self:words(this.context.ecx.add(8),2),point:[u32(args[0]),u32(args[1])],range:u32(args[7]),arrival:args[2].toUInt32(),cantpath:args[3].toUInt32(),flag:args[6].toUInt32()});
    }});
    Interceptor.attach(base.add(0x16c150), {onEnter() {
        this.record=active;
        if(this.record)row('spell-owner-clock',{g:this.context.ecx.toString(),phase:'begin'});
    },onLeave() {if(this.record)row('spell-owner-clock',{phase:'end'});}});
}});
