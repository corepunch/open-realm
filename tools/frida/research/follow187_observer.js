// Appended to the existing read-only pursuit observer. No writes or game calls.
Process.attachModuleObserver({onAdded(module) {
    if (module.name.toLowerCase() !== 'game.dll') return;
    const base = module.base, pe = base.add(base.add(0x3c).readU32());
    if (Process.pointerSize !== 4 || pe.add(8).readU32() !== config.timestamp || pe.add(80).readU32() !== config.imageSize)
        throw new Error('Follow187 PE mismatch');
    const counter = () => u32(base.add(0xd53a48).readPointer().add(0x538));
    const refs = path => ({path: path.toString(), self: path.add(0xa0).readPointer().toString(),
                          target: path.add(0xa4).readPointer().toString()});
    const record = (event, data) => {if (active) {bump(event); emit(event, {c: counter(), ...data});}};
    Interceptor.attach(base.add(0x16ce10), {onEnter() {
        const group = this.context.ecx;
        record('group-path-regions', {group: group.toString(), ...refs(group.add(0x3c).readPointer())});
    }});
    Interceptor.attach(base.add(0x166c30), {onEnter() {
        record('coarse-path-regions', refs(this.context.ecx));
    }});
    Interceptor.attach(base.add(0x168b60), {onEnter(args) {
        record('set-self-region', {path: this.context.ecx.toString(), region: args[0].toString(),
                                  caller: this.returnAddress.sub(base).toUInt32()});
    }});
}});
