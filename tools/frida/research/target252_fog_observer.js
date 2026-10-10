// Original 1.27 DLL, entry/exit observations only. Never writes or invokes game code.
function installTargetExtension(base, hook, out, counter) {
    const word = p => p.readU32() >>> 0;
    for (const [rva, name] of [[0x251ac0, 'fog-compose'], [0x289260, 'fog-event'],
                             [0x28ba80, 'fog-arm'], [0x1fd040, 'fog-destroy'],
                             [0x1fe600, 'fog-start'], [0x1fe620, 'fog-stop']]) {
        hook(rva, {
            onEnter(args) {
                this.row = {c: counter(), caller: this.returnAddress.sub(base).toUInt32(),
                    self: this.context.ecx.toString(), period: word(base.add(0xd69474))};
                if (name === 'fog-arm') this.row.enabled = args[0].toUInt32();
                if (name === 'fog-event' || name === 'fog-arm') {
                    this.row.control = [];
                    for (let i = 0; i < 8; ++i) this.row.control.push(word(this.context.ecx.add(0x280 + i * 4)));
                }
                out(name, this.row);
            },
            onLeave() {out(name + '-end', {c: counter()});}
        });
    }
}
