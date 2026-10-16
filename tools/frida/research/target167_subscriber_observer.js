// Read-only Move target-loss subscription order; no calls and no writes.
function installTarget167Subscribers(base, hook, out, counter) {
    hook(0x0725b0, {onEnter(args) {
        if(args[0].toUInt32()!==0xd01a4)return;
        const callback=args[2];
        let owner=null;
        try {owner=callback.add(0x30).readPointer().toString();} catch(e) {}
        out('target-subscribe',{c:counter(),target:this.context.ecx.toString(),
            remapped:args[1].toUInt32(),callback:callback.toString(),owner});
    }});
    hook(0x0728c0, {onEnter(args) {
        if(args[0].toUInt32()!==0xd01a4)return;
        out('target-unsubscribe',{c:counter(),target:this.context.ecx.toString(),callback:args[1].toString()});
    }});
    hook(0x5ff490, {onEnter(args) {
        let owner=null;
        try {owner=this.context.ecx.add(0x30).readPointer().toString();} catch(e) {}
        out('target-handler-enter',{c:counter(),callback:this.context.ecx.toString(),owner,
            target:args[0].add(0xc).readPointer().toString()});
    }});
}
