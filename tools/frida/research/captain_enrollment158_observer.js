// Read-only call boundaries. No original invocations and no memory writes.
let installed=false,recording=true,tick=0,index=-1;
const counts={};
const emit=(event,data={})=>{if(recording)send({event,...data});};
const bump=k=>counts[k]=(counts[k]||0)+1;
function install(module){
    if(installed||module.name.toLowerCase()!=='game.dll')return;
    const base=module.base,pe=base.add(base.add(0x3c).readU32());
    if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('wrong PE');
    installed=true;
    const hook=(r,c)=>Interceptor.attach(base.add(r),c);
    const interested=u=>!u.isNull()&&u.add(0x30).readU32()===0x68524130;
    const words=(u,offset,n)=>Array.from({length:n},(_,i)=>u.add(offset+4*i).readU32());
    const units=new Map();
    const unitState=u=>({unit:u.toString(),flags5c:u.add(0x5c).readU32(),separationDisableDepth:u.add(0x198).readU32(),userHead:words(u,0x19c,2),userTail:words(u,0x1a8,2),userCount:u.add(0x1b4).readU32(),taskHead:words(u,0x174,2)});
    const captainState=c=>({captain:c.toString(),flags:c.add(0x6c).readU32(),state:c.add(0x64).readU32(),counts:words(c,0xbc,5),point:[c.add(0x58).readU32(),c.add(0x60).readU32()]});
    const townState=t=>({town:t.toString(),flags:t.isNull()?null:t.add(0x2d0).readU32()});
    hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith('RSG'))return;
        const m=/tick=(\d+)/.exec(value);if(m)tick=Number(m[1]);const n=/ u=(\d+)/.exec(value);if(n)index=Number(n[1]);emit('marker',{value});if(value.includes('label=sample')&&index===0)emit('snapshot',{tick,units:Array.from(units.values()).map(unitState)});}});
    hook(0x9ccdb0,{onEnter(args){this.u=ptr(args[0].toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,...townState(ptr(this.context.ecx.toString())),before:unitState(this.u)};},
        onLeave(){if(this.watch){bump('temporary');emit('temporary',{...this.row,after:unitState(this.u)});}}});
    for(const [rva,name]of [[0x9cf680,'attach'],[0x9d5610,'detach']])hook(rva,{onEnter(args){const u=ptr(args[0].toString());if(!interested(u))return;
        bump(name);emit(name,{tick,index,captain:this.context.ecx.toString(),...unitState(u)});}});
    hook(0x693450,{onEnter(){const u=ptr(this.context.ecx.toString());if(!interested(u))return;units.set(u.toString(),u);bump('append-ai-order');emit('append-ai-order',{tick,index,...unitState(u)});}});
    hook(0x9d86f0,{onEnter(args){this.u=ptr(args[1].toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,unit:this.u.toString(),out:args[0]};},onLeave(){if(this.watch){bump('range');const out=this.row.out;delete this.row.out;emit('range',{...this.row,value:out.readU32()});}}});
    hook(0x6803f0,{onEnter(args){this.u=ptr(this.context.ecx.toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,mode:args[0].toUInt32(),before:unitState(this.u)};},onLeave(){if(this.watch){bump('stop-admission');emit('stop-admission',{...this.row,after:unitState(this.u)});}}});
    hook(0x693490,{onEnter(args){this.u=ptr(this.context.ecx.toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,caller:this.returnAddress.sub(base).toUInt32(),order:args[0].add(0x24).readU32(),point:words(args[0],0x48,1).concat(words(args[0],0x50,1)),before:unitState(this.u)};},onLeave(){if(this.watch){bump('append-user');emit('append-user',{...this.row,after:unitState(this.u)});}}});
    hook(0x9bbb90,{onEnter(args){bump('policy');emit('policy',{tick,value:args[0].toUInt32()});}});
    hook(0x9d8a90,{onEnter(args){const u=ptr(args[0].toString());if(!interested(u))return;bump('idle-member');emit('idle-member',{tick,index,...captainState(ptr(this.context.ecx.toString())),...unitState(u)});}});
    hook(0x9d87d0,{onEnter(args){this.u=ptr(args[0].toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,...captainState(ptr(this.context.ecx.toString())),order:args[1].toUInt32(),target:args[2].toString(),point:[args[3].readU32(),args[4].readU32()],before:unitState(this.u)};},onLeave(){if(this.watch){bump('reissue');emit('reissue',{...this.row,after:unitState(this.u)});}}});
    hook(0x9d8c70,{onEnter(args){this.c=ptr(this.context.ecx.toString());this.row={tick,index,...captainState(this.c),source:args[0].toString(),target:args[1].toString()};},onLeave(ret){bump('reachability');emit('reachability',{...this.row,result:ret.toUInt32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={status(){return{installed,counts};},finish(){recording=false;return{installed,counts};}};
