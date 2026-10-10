// Read-only argument/handle observations. No calls or writes into the game.
function installTargetExtension(base, hook, out, counter) {
    const word = p => p.readU32() >>> 0;
    for (const [rva, name] of [[0x251ac0, 'fog-compose'], [0x1fe600, 'fog-start'], [0x1fe620, 'fog-stop'], [0x1fd040, 'fog-destroy']]) {
        hook(rva, {onEnter(args) {
            const row = {c: counter(), period: word(base.add(0xd69474))};
            if (name === 'fog-compose') this.map = this.context.ecx;
            if (name !== 'fog-compose') row.handle = args[0].toUInt32();
            out(name, row);
        }, onLeave() {
            if (!this.map) return;
            const m = this.map, width = word(m.add(0x60)), stride = word(m.add(0x64)), height = word(m.add(0x6c));
            let visible=0,masked=0;
            for (let y=0;y<height;y++) for (let x=0;x<width;x++) {
                visible += (m.add(0x30).readPointer().add(2*(y*stride+x)).readU16() & 1) !== 0;
                masked += (m.add(0x2c).readPointer().add(2*(y*stride+x)).readU16() & 1) !== 0;
            }
            out('fog-plane', {c:counter(),width,stride,height,visible,masked});
        }});
    }
    hook(0x1fb730, {onEnter(args) {
        this.row = {c: counter(), player: args[0].toUInt32(), state: args[1].toUInt32(),
            x: word(args[2]), y: word(args[3]), radius: word(args[4]),
            shared: args[5].toUInt32(), after: args[6].toUInt32()};
    }, onLeave(ret) {out('fog-create', {...this.row, handle: ret.toUInt32()});}});
    hook(0x1ed050, {onLeave(ret) {
        if (!ret.isNull()) out('fog-resolve', {c: counter(), flags: word(ret.add(0x20)), self: ret.toString()});
    }});
}
