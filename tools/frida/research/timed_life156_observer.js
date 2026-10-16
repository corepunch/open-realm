// Read-only original function boundaries; no native calls or memory writes.
let installed=false,recording=true,tick=0,index=-1;const counts={};
const emit=(event,data={})=>{if(recording)send({event,...data});};
const bump=k=>counts[k]=(counts[k]||0)+1;
function install(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('wrong PE');
 installed=true;const hook=(r,c)=>Interceptor.attach(base.add(r),c),rel=p=>p.sub(base).toUInt32();
 const flags=u=>({flags5c:u.add(0x5c).readU32(),flags64:u.add(0x64).readU32(),flags248:u.add(0x248).readU32()});
 const interested=u=>!u.isNull()&&u.add(0x30).readU32()===0x68524130;
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith('RSG '))return;tick=Number(/tick=(\d+)/.exec(value)[1]);const m=/ u=(\d+)/.exec(value);if(m)index=Number(m[1]);emit('marker',{value});}});
 hook(0x48b930,{onEnter(args){this.u=ptr(this.context.ecx.toString());this.watched=interested(this.u);if(!this.watched)return;this.row={tick,index,buff:args[0].toUInt32(),duration:args[1].readU32(),before:flags(this.u)};},onLeave(){if(this.watched){bump('native');emit('native',{...this.row,after:flags(this.u)});}}});
 for(const [rva,name]of [[0x489b30,'BTLF'],[0x489650,'BUan'],[0x489720,'BFig'],[0x4897f0,'BEfn'],[0x4898c0,'Bhwd'],[0x489990,'Bplg'],[0x489a60,'Brai'],[0x489c00,'BHwe']])hook(rva,{onLeave(ret){if(ret.isNull())return;const b=ptr(ret.toString()),v=b.readPointer();bump('factory');emit('factory',{tick,index,name,vtable:rel(v),initialize:rel(v.add(0x31c).readPointer())});}});
 hook(0x530750,{onEnter(args){this.u=ptr(args[0].toString());this.watched=interested(this.u);if(this.watched)this.row={tick,index,duration:args[1].readU32(),before:flags(this.u)};},onLeave(){if(this.watched){bump('initialize');emit('initialize',{...this.row,after:flags(this.u)});}}});
}
Process.attachModuleObserver({onAdded:install});rpc.exports={status(){return{installed,counts};},finish(){recording=false;return{installed,counts};}};
