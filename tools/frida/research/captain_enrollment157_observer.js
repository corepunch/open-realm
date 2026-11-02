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
    const unitState=u=>({unit:u.toString(),flags5c:u.add(0x5c).readU32()});
    const townState=t=>({town:t.toString(),flags:t.isNull()?null:t.add(0x2d0).readU32()});
    hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith('RSG'))return;
        const m=/tick=(\d+)/.exec(value);if(m)tick=Number(m[1]);const n=/ u=(\d+)/.exec(value);if(n)index=Number(n[1]);emit('marker',{value});}});
    hook(0x9ccdb0,{onEnter(args){this.u=ptr(args[0].toString());this.watch=interested(this.u);if(this.watch)this.row={tick,index,...townState(ptr(this.context.ecx.toString())),before:unitState(this.u)};},
        onLeave(){if(this.watch){bump('temporary');emit('temporary',{...this.row,after:unitState(this.u)});}}});
    for(const [rva,name]of [[0x9cf680,'attach'],[0x9d5610,'detach']])hook(rva,{onEnter(args){const u=ptr(args[0].toString());if(!interested(u))return;
        bump(name);emit(name,{tick,index,captain:this.context.ecx.toString(),...unitState(u)});}});
    hook(0x693450,{onEnter(){const u=ptr(this.context.ecx.toString());if(!interested(u))return;bump('append-ai-order');emit('append-ai-order',{tick,index,...unitState(u)});}});
    hook(0x9bbb90,{onEnter(args){bump('policy');emit('policy',{tick,value:args[0].toUInt32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={status(){return{installed,counts};},finish(){recording=false;return{installed,counts};}};
