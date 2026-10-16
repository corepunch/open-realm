// Read-only Patrol endpoint/task producer snapshots. No game calls or writes.
function installPathPatrolLifetime(base, hook) {
    const pair = p => [p.readU32(), p.add(4).readU32()];
    const point = (p, x, y) => [p.add(x).readU32(), p.add(y).readU32()];
    const state = unit => ({unit:unit.toString(), identity:pair(unit.add(0xc)),
        publicHead:pair(unit.add(0x19c)), publicTail:pair(unit.add(0x1a8)),
        publicCount:unit.add(0x1b4).readU32(), taskHead:pair(unit.add(0x174))});
    const order = p => ({identity:pair(p.add(0xc)), command:p.add(0x24).readU32(),
        player:p.add(0x28).readU32(), successor:pair(p.add(0x2c)),
        primary:point(p,0x48,0x50), continuation:point(p,0x5c,0x64)});
    hook(0x5fdff0, {
        onEnter(args) {
            this.ability=this.context.ecx;
            this.unit=this.ability.add(0x30).readPointer();
            this.row={before:state(this.unit), order:order(args[0].add(0xc).readPointer())};
        },
        onLeave() {emit('patrol-create-tasks',{...this.row,after:state(this.unit)});}
    });
    hook(0x5fff70, {
        onEnter(args) {
            this.unit=this.context.ecx.add(0x30).readPointer();
            const task=args[0].add(0xc).readPointer();
            this.row={before:state(this.unit),task:pair(task.add(0xc)),
                eventCode:task.add(0x30).readU32(),primary:point(task,0x38,0x40),
                continuation:point(task,0x54,0x5c)};
        },
        onLeave() {emit('patrol-continuation-task',{...this.row,after:state(this.unit)});}
    });
    hook(0x690fa0, {
        onEnter(args) {
            this.observe=this.context.ecx.toUInt32()===0xd0017;
            if (this.observe) this.row={command:this.context.ecx.toUInt32(),
                player:this.context.edx.toUInt32(),target:args[0].toString(),
                primary:[args[1].readU32(),args[2].readU32()],
                continuation:[args[3].readU32(),args[4].readU32()],
                caller:this.returnAddress.sub(base).toUInt32()};
        },
        onLeave(result) {
            if (this.observe) emit('patrol-order-factory',{...this.row,order:order(result)});
        }
    });
    hook(0x693490, {
        onEnter(args) {
            this.observe=args[0].add(0x24).readU32()===0xd0017;
            if (this.observe) {
                this.unit=this.context.ecx;
                this.row={before:state(this.unit),order:order(args[0]),
                    caller:this.returnAddress.sub(base).toUInt32()};
            }
        },
        onLeave() {if(this.observe)emit('patrol-append',{...this.row,after:state(this.unit)});}
    });
}
