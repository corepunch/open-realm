// Read-only original primary-request entry/rearm: guard events only.
function installTargetExtension(base,hook,out,counter) {
    const word=p=>p.readU32()>>>0;
    const controls=new Map();
    const state=p=>({request:p.toString(),deadline:word(p.add(4)),period:word(p.add(8)),
        clock:p.add(12).readPointer().toString(),flags:word(p.add(16)),serial:word(p.add(20)),
        receiver:p.add(24).readPointer().toString(),value:word(p.add(28)),code:controls.get(p.add(24).readPointer().toString())});
    const guard=p=>controls.has(p.add(24).readPointer().toString());
    hook(0x0608d0,{onEnter(args){
        const code=args[1].toUInt32();
        if(code===0xd01ad || code===0xd01ae)controls.set(this.context.ecx.toString(),code);
    }});
    hook(0x0542d0,{onEnter(){
        this.p=this.context.ecx;if(!guard(this.p))return;
        this.row={c:counter(),before:state(this.p)};
    },onLeave(){if(this.row)out('guard-execute255',{...this.row,after:state(this.p)});}});
    hook(0x053630,{onEnter(args){
        this.p=args[0];if(!guard(this.p))return;
        this.row={c:counter(),before:state(this.p)};
    },onLeave(){if(this.row)out('guard-rearm255',{...this.row,after:state(this.p)});}});
}
