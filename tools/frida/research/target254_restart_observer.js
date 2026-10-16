// Read-only restart producer and call-chain observations on the original DLL.
function installTargetExtension(base, hook, out, counter) {
    const word = p => p.readU32() >>> 0;
    const words = (p,n) => Array.from({length:n},(_,i)=>word(p.add(i*4)));
    const chain = ctx => Thread.backtrace(ctx,Backtracer.ACCURATE).filter(p => p.compare(base)>=0 && p.compare(base.add(0xe6e000))<0).map(p=>p.sub(base).toString());
    for (const off of [0x497330,0x49d220,0x499030,0x49e880,0x497c50]) hook(off,{onEnter(args) {
        this.self=this.context.ecx;
        this.row={c:counter(),fn:off,self:this.self.toString(),flags:word(this.self.add(0x20)),anchor:[word(this.self.add(0x2ac)),word(this.self.add(0x2b4))],chain:chain(this.context)};
    },onLeave() {out('guard-producer254',{...this.row,flagsAfter:word(this.self.add(0x20)),anchorAfter:[word(this.self.add(0x2ac)),word(this.self.add(0x2b4))]});}});
    hook(0x495610,{onEnter() {this.row={c:counter(),self:this.context.ecx.toString(),range:word(this.context.ecx.add(0x27c)),anchor:[word(this.context.ecx.add(0x2ac)),word(this.context.ecx.add(0x2b4))],chain:chain(this.context)};},onLeave(ret) {out('guard-range254',{...this.row,result:ret.toUInt32()});}});
    hook(0x0608d0,{onEnter(args) {
        const code=args[1].toUInt32();if(code!==0xd01ad && code!==0xd01ae)return;
        this.self=this.context.ecx;
        this.row={c:counter(),control:this.self.toString(),delay:word(args[0]),code,receiver:args[2].toString(),periodic:args[3].toUInt32(),chain:chain(this.context)};
    },onLeave() {if(this.row)out('guard-arm254',{...this.row,controlWords:words(this.self,10)});}});
    hook(0x5fdaa0,{onEnter(args) {
        const c=counter(); if(c<1470 || c>1500)return;
        out('move-dispatch254',{c,self:this.context.ecx.toString(),code:word(args[0].add(8)),caller:this.returnAddress.sub(base).toUInt32(),chain:chain(this.context)});
    }});
    hook(0x5ffb60,{onEnter(args) {
        const e=args[0],task=e.add(0xc).readPointer();
        out('point-task254',{c:counter(),self:this.context.ecx.toString(),unit:this.context.ecx.add(0x30).readPointer().toString(),code:word(e.add(8)),task:task.toString(),point:words(task.add(0x38),5),policy:[args[1].toUInt32(),args[2].toUInt32()],chain:chain(this.context)});
    }});
    hook(0x05b970,{onEnter(args) {
        out('point-start254',{c:counter(),self:this.context.ecx.toString(),point:[word(args[0]),word(args[1])],handler:args[4].toString(),range:word(args[7]),chain:chain(this.context)});
    }});
    hook(0x171340,{onEnter(args) {
        this.m=this.context.ecx;this.c=counter();
        this.row={c:this.c,m:this.m.toString(),pos:words(this.m.add(0x78),2),vel:words(this.m.add(0x80),2),clock:word(this.m.add(0x8c)),args:Array.from({length:7},(_,i)=>args[i].toUInt32()),chain:chain(this.context)};
    },onLeave() {
        out('point-stop254',{...this.row,after:words(this.m.add(0x78),6)});
    }});
}
