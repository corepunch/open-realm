// Read-only target-loss snapshots and actual retail caller stacks.
function installPathFollowLifetime(base, hook) {
    const pair = p => [p.readU32(), p.add(4).readU32()];
    const state = p => ({unit:p.toString(), flags:p.add(0x5c).readU32(),
        publicHead:pair(p.add(0x19c))});
    const stack = context => Thread.backtrace(context, Backtracer.ACCURATE)
        .filter(p => p.compare(base)>=0 && p.compare(base.add(config.imageSize))<0)
        .slice(0,16).map(p => p.sub(base).toUInt32());
    hook(0x5fbea0, {
        onEnter() {
            this.ability=this.context.ecx; this.unit=this.ability.add(0x30).readPointer();
            this.before=state(this.unit); this.retained=pair(this.ability.add(0xcc));
            this.callers=stack(this.context);
        },
        onLeave() {
            emit("follow-target-clear", {ability:this.ability.toString(),
                before:this.before,after:state(this.unit),retainedBefore:this.retained,
                retainedAfter:pair(this.ability.add(0xcc)),callers:this.callers});
        }
    });
    hook(0x651010, {
        onEnter(args) {
            this.unit=this.context.ecx;
            this.before=state(this.unit); this.callers=stack(this.context);
            this.players=[args[0].toInt32(),args[1].toInt32()];
        },
        onLeave() {
            emit('follow-loss-source',{before:this.before,after:state(this.unit),
                callers:this.callers,players:this.players});
        }
    });
    hook(0x5ff490, {
        onEnter(args) {
            this.ability=this.context.ecx;
            this.unit=this.ability.add(0x30).readPointer();
            this.before=state(this.unit); this.callers=stack(this.context);
            this.target=args[0].add(0xc).readPointer().toString();
            this.retained=pair(this.ability.add(0xcc));
        },
        onLeave() {
            emit('follow-loss-owner',{ability:this.ability.toString(),target:this.target,
                before:this.before,after:state(this.unit),retainedBefore:this.retained,
                retainedAfter:pair(this.ability.add(0xcc)),callers:this.callers});
        }
    });
}
