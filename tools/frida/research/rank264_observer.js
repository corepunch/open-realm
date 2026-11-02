// Original target ranking observer: read-only hooks, no game calls or writes.
let installed=false,active=false,tick=0,seq=0,depth=0;const counts={},ids=new Map();
function id(p){if(p.isNull())return -1;const k=p.toString();if(!ids.has(k))ids.set(k,ids.size);return ids.get(k);}
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const rva=p=>p.sub(base).toUInt32().toString(16),hook=(r,h)=>Interceptor.attach(base.add(r),h);
 const unit=u=>({identity:id(u),rawcode:u.add(0x30).readU32(),flags:u.add(0x5c).readU32(),widget:u.add(0x20).readU32(),e4:u.add(0xe4).readU32(),class248:u.add(0x248).readU32(),movement:u.add(0x1fc).readU32(),forced:u.add(0x200).readS32(),owner:u.add(0x58).readU32(),attack:u.add(0x1e8).readPointer().isNull()?null:{pointer:u.add(0x1e8).readPointer().toString(),flags:u.add(0x1e8).readPointer().add(0x20).readU32(),prevention:u.add(0x1e8).readPointer().add(0x3c).readS32(),target:[u.add(0x1e8).readPointer().add(0x6c).readU32(),u.add(0x1e8).readPointer().add(0x70).readU32()]},move:u.add(0x1ec).readPointer().toString()});
 emit('module',{base:base.toString(),minimumRange:base.add(0xd6bf64).readU32()});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);if(value.includes('label=owner-before'))active=true;emit('marker',{value});if(value.includes('label=owner-after'))active=false;}});
 hook(0x49e130,{onEnter(args){if(!active)return;this.track=true;this.a=this.context.ecx;depth++;emit('notify',{actor:unit(this.a.add(0x30).readPointer()),candidate:unit(args[0].add(0xc).readPointer()),ability:this.a.toString(),flags:this.a.add(0x20).readU32(),current:[this.a.add(0x6c).readU32(),this.a.add(0x70).readU32()]});},onLeave(){if(this.track)depth--;}});
 hook(0x49bc40,{onEnter(){if(active&&depth)emit('exempt',{actor:id(this.context.ecx.add(0x30).readPointer())});}});
 hook(0x49e3a0,{onEnter(args){if(!active||!depth)return;this.track=true;this.a=this.context.ecx;emit('compare',{actor:id(this.a.add(0x30).readPointer()),candidate:unit(args[0]),current:[this.a.add(0x6c).readU32(),this.a.add(0x70).readU32()]});},onLeave(v){if(this.track)emit('compare-end',{actor:id(this.a.add(0x30).readPointer()),accepted:v.toUInt32(),current:[this.a.add(0x6c).readU32(),this.a.add(0x70).readU32()]});}});
 hook(0x49d680,{onEnter(args){if(!active||!depth)return;this.track=true;this.a=this.context.ecx;emit('rank',{actor:id(this.a.add(0x30).readPointer()),candidate:unit(args[0]),bound:[args[1].toUInt32(),args[2].toUInt32()],target:[this.a.add(0x6c).readU32(),this.a.add(0x70).readU32()]});},onLeave(v){if(this.track)emit('rank-end',{actor:id(this.a.add(0x30).readPointer()),key:[v.toUInt32(),this.context.edx.toUInt32()]});}});
 hook(0x05b580,{onEnter(args){if(!active||!depth)return;this.track=true;this.range=args[0].readU32();this.mode=args[2].toUInt32();this.caller=rva(this.returnAddress);},onLeave(v){if(this.track)emit('range',{range:this.range,mode:this.mode,caller:this.caller,accepted:v.toUInt32()});}});
 for(const address of [0x5fb040,0x48e260,0x495b90,0x699b80,0x68bd60,0x48e2c0,0x496b80,0x68c250])hook(address,{onEnter(){if(!active||!depth)return;this.track=true;this.receiver=this.context.ecx.toString();},onLeave(v){if(this.track)emit('predicate',{address:address.toString(16),receiver:this.receiver,result:v.toUInt32()});}});
 hook(0x05b1c0,{onEnter(){if(!active||!depth)return;this.track=true;this.out=this.context.esp.add(4).readPointer();this.caller=rva(this.returnAddress);},onLeave(){if(this.track)emit('distance',{caller:this.caller,word:this.out.readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,readOnly:true,counts,depth};}};
