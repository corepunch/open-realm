// Read-only can’t-path owner and replacement-order dispatch.
let installed=false,active=false,base,tick=0,scene=0,seq=0;const counts={};let boardProbeUnit=null;
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mine=u=>!u.isNull()&&[0x68666f6f,0x6e7a6570,0x68626f74].includes(u.add(0x30).readU32());
const unit=u=>({identity:pair(u.add(12)),flags:u.add(0x5c).readU32(),widgetFlags:u.add(0x20).readU32(),head:pair(u.add(0x19c)),task:pair(u.add(0x174)),ai:pair(u.add(0x304)),attackTarget:u.add(0x1e8).readPointer().isNull()?null:pair(u.add(0x1e8).readPointer().add(0x6c))});
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 // Guard the full owner/command setup before attaching: 0x693460 is inside
 // MOV ECX,0xd0006 at 0x69345f, not a function or instruction boundary.
 const appendPrefix='568bf16a008b06ff90ec0000008bd0b906000d00';
 const appendBytes=Array.from(new Uint8Array(base.add(0x693450).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
 if(appendBytes!==appendPrefix)throw Error('AI append entry bytes differ');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 for(const [r,prefix,table] of [[0x58edd0,'558bec83ec4c53568bf15789',null],
                               [0x692120,'558bec6aff68',0xa582d0],
                               [0x059ca0,'558bec6aff68',0x9dd95b]]) {
  const bytes=Array.from(new Uint8Array(base.add(r).readByteArray(prefix.length/2))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix || (table!==null&&!base.add(r+6).readPointer().equals(base.add(table))))throw Error('shore/point entry differs');
 }
 const transientBytes=Array.from(new Uint8Array(base.add(0x691c70).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
 if(transientBytes!=='558bec568bf1f7465c000100000f859400000081')throw Error('transient insertion entry bytes differ');
 hook(0x691c70,{onEnter(a){this.u=active&&mine(this.context.ecx)?this.context.ecx:null;if(this.u){this.before=this.u.add(0x1b4).readU32();this.head=pair(this.u.add(0x19c));this.flags=[a[1].toUInt32(),a[2].toUInt32()];this.caller=this.returnAddress.sub(base).toString();}},onLeave(){if(this.u)emit('transient-insert',{unit:pair(this.u.add(12)),before:this.before,after:this.u.add(0x1b4).readU32(),headBefore:this.head,headAfter:pair(this.u.add(0x19c)),flags:this.flags,caller:this.caller});}});

 const distancePrefix=Array.from(new Uint8Array(base.add(0x05b1c0).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
 if(distancePrefix!=='558bec83ec188b510c8b490856e85e93ffff8b0d')throw Error('committed distance entry differs');
 hook(0x05b1c0,{onEnter(a){const caller=this.returnAddress.sub(base).toUInt32();this.keep=active&&caller>=0x5856a0&&caller<0x585a00;if(this.keep){this.output=a[0];this.prediction=a[3].toUInt32();this.mover=pair(this.context.ecx.add(8));}},onLeave(){if(this.keep)emit('board-distance',{mover:this.mover,bits:this.output.readU32(),prediction:this.prediction});}});
 for(const [r,name,prefix] of [[0x5fb4c0,'recover-board','558bec83ec0c837d0800538bd97411e8ccf2ffff'],[0x5fb620,'recover-drop','558bec83ec245356578bf98b773085f67507e889'],[0x5fb6e0,'recover-load','56578bf98b773085f67507e8d0a5a5ff8bf0f746']]) {
  const bytes=Array.from(new Uint8Array(base.add(r).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('recovery helper entry differs');
  hook(r,{onEnter(a){this.owner=active?this.context.ecx.add(0x30).readPointer():null;if(this.owner){this.reason=r===0x5fb4c0?a[0].toUInt32():null;emit(name+'-enter',{unit:unit(this.owner),reason:this.reason,caller:this.returnAddress.sub(base).toString()});}},onLeave(ret){if(this.owner)emit(name+'-leave',{unit:unit(this.owner),result:ret.toUInt32()});}});
 }
 hook(0x48cb80,{onEnter(){this.keep=active&&this.returnAddress.equals(base.add(0x5fb715));},onLeave(ret){if(this.keep)emit('recover-load-ability',{present:!ret.isNull(),flags:ret.isNull()?null:ret.add(0x20).readU32()});}});
 hook(0x69c6a0,{onEnter(){const c=this.returnAddress.sub(base).toUInt32();this.keep=active&&c>=0x5fa7a0&&c<0x5fa942;},onLeave(ret){if(this.keep)emit('arrival-task-head',{present:!ret.isNull(),code:ret.isNull()?null:ret.add(0x30).readU32()});}});
 hook(0x6958b0,{onEnter(){if(active&&mine(this.context.ecx))emit('arrival-release800',{unit:unit(this.context.ecx),caller:this.returnAddress.sub(base).toString()});}});
 const targetTaskBytes=Array.from(new Uint8Array(base.add(0x6926b0).readByteArray(20)));
 if(!base.add(0x6926b6).readPointer().equals(base.add(0xa58410)))throw Error('target task exception-table relocation differs');
 targetTaskBytes.splice(6,4,0x10,0x84,0xa5,0x6f);
 const targetTaskPrefix=targetTaskBytes.map(v=>v.toString(16).padStart(2,'0')).join('');
 if(targetTaskPrefix!=='558bec6aff681084a56f64a1000000005083ec40')throw Error('target task entry differs');
 hook(0x6926b0,{onEnter(a){if(active&&mine(this.context.ecx))emit('target-task-create',{unit:unit(this.context.ecx),code:a[0].toUInt32(),target:a[1].isNull()?null:pair(a[1].add(12)),rangeBits:a[2].isNull()?null:a[2].readU32(),caller:this.returnAddress.sub(base).toString()});}});
 const selectPrefix=Array.from(new Uint8Array(base.add(0x5856a0).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
 if(selectPrefix!=='558bec83ec5c538bd95657895dec8b43308945f0')throw Error('Cargo selector entry bytes differ');
 hook(0x5856a0,{onEnter(){this.keep=active;this.caller=this.returnAddress.sub(base).toString();},onLeave(ret){if(this.keep)emit('board-nearest',{target:ret.isNull()?null:pair(ret.add(12)),caller:this.caller});}});

 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('O346 '))return;if(value.includes('label=start')){active=true;emit('drop-threshold',{bits:base.add(0xd6fb44).readU32(),value:base.add(0xd6fb44).readFloat()});}tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);emit('marker',{value});if(value.includes('label=carrier_removed')&&boardProbeUnit){const move=boardProbeUnit.add(0x1ec).readPointer();emit('removed-carrier-context',{unit:unit(boardProbeUnit),moveFlags:move.isNull()?null:move.add(0x20).readU32(),retainedTarget:move.isNull()?null:pair(move.add(0x84))});}if(value.includes('label=complete'))active=false;}});
 for(const [r,name] of [[0x603110,'cant-path'],[0x5fb190,'recover'],[0x5fa7a0,'arrival']]){
  hook(r,{onEnter(){if(!active)return;this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();if(!mine(this.u)){this.u=null;return;}emit(name+'-enter',{unit:unit(this.u),flags:this.a.add(0x20).readU32(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u),flags:this.a.add(0x20).readU32()});}});
 }
 for(const [r,name] of [[0x6802f0,'ai-immediate'],[0x693450,'ai-append']])hook(r,{onEnter(){if(active&&mine(this.context.ecx))emit(name,{unit:unit(this.context.ecx),caller:this.returnAddress.sub(base).toString()});}});
 hook(0x687020,{onEnter(){this.u=active&&mine(this.context.ecx)?this.context.ecx:null;},onLeave(ret){if(this.u&&!ret.isNull())emit('ai-record',{unit:unit(this.u),flags:ret.add(0x28).readU32()});}});
 hook(0x603d70,{onEnter(){if(!active)return;this.traced=true;emit('find-transport-enter',{caller:this.returnAddress.sub(base).toString(),rangeBits:base.add(0xd6fbc4).readU32()});},onLeave(ret){if(this.traced)emit('find-transport-leave',{found:!ret.isNull()});}});
 hook(0x9c0b00,{onEnter(a){if(!active||!mine(a[0]))return;emit('guard-return',{unit:unit(a[0]),point:[this.context.ecx.add(0x28).readU32(),this.context.ecx.add(0x30).readU32()],caller:this.returnAddress.sub(base).toString()});}});
 hook(0x602ad0,{onEnter(){if(!active)return;this.u=this.context.ecx;this.c=this.context.edx;emit('alternative-enter',{candidate:unit(this.u),type:this.u.add(0x30).readU32(),invulnerable:this.u.add(0xec).readU32(),best:this.c.add(8).readU32()});},onLeave(){if(this.u)emit('alternative-leave',{candidate:pair(this.u.add(12)),selected:this.c.add(12).readPointer().equals(this.u),best:this.c.add(8).readU32()});}});
 hook(0x607950,{onEnter(){if(!active)return;this.u=this.context.ecx;this.p=this.context.edx;emit('pick-enter',{unit:unit(this.u),point:[this.p.readU32(),this.p.add(4).readU32(),this.p.add(8).readU32()]});},onLeave(ret){if(this.u)emit('pick-leave',{found:!ret.isNull(),carrier:ret.isNull()?null:unit(ret)});}});
 hook(0x610820,{onEnter(){if(!active)return;this.a=this.context.ecx;this.q=this.context.edx;this.u=this.a.add(0x30).readPointer();emit('carrier-enter',{unit:unit(this.u),disabled:this.a.add(0x3c).readU32(),queryOwner:this.q.add(4).readU32(),ground:this.u.add(0x200).readU32(),statFns:[this.u.readPointer().add(0x130).readPointer().sub(base).toString(),this.u.readPointer().add(0x12c).readPointer().sub(base).toString()]});},onLeave(ret){if(this.u)emit('carrier-leave',{ret:ret.toUInt32(),selected:this.q.add(40).readPointer().equals(this.u),best:this.q.add(44).readU32()});}});
 hook(0x58eb60,{onEnter(a){if(!active)return;this.a=this.context.ecx;this.x=a[1];this.y=a[2];emit('drop-enter',{unit:unit(this.a.add(0x30).readPointer()),point:[this.x.readU32(),this.y.readU32()]});},onLeave(ret){if(this.a)emit('drop-leave',{ret:ret.toUInt32(),point:[this.x.readU32(),this.y.readU32()]});}});
 for(const [r,name] of [[0x685270,'carrier-life-limit'],[0x685170,'carrier-life']])hook(r,{onEnter(){const caller=this.returnAddress.sub(base).toUInt32();this.traced=active&&caller>=0x610820&&caller<0x6109a3;},onLeave(ret){if(this.traced)emit(name,{bits:ret.readU32()});}});
 hook(0x610710,{onEnter(a){if(!active)return;this.u=this.context.ecx;this.p=a[1];emit('destination-enter',{unit:unit(this.u),command:a[0].toUInt32(),point:[this.p.readU32(),this.p.add(4).readU32()]});},onLeave(ret){if(this.u)emit('destination-leave',{accepted:ret.toUInt32()});}});
 const sizePrefix='558bec837d080056578bf974356a016a016a006a';
 const sizeBytes=Array.from(new Uint8Array(base.add(0x48d330).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
 if(sizeBytes!==sizePrefix)throw Error('cargo-size entry bytes differ');
 hook(0x48d330,{onEnter(a){this.u=active&&mine(this.context.ecx)?this.context.ecx:null;if(this.u){this.morph=a[0].toInt32();this.caller=this.returnAddress.sub(base).toString();}},onLeave(ret){if(this.u)emit('cargo-size',{identity:pair(this.u.add(12)),morph:this.morph,size:ret.toInt32(),caller:this.caller});}});
 for(const [r,prefix] of [[0x5ae130,'558bec837d08ff750c8b01ff90240300005dc204'],[0x5806f0,'558bec51568bf18d45fcff765050e84d44e9ff8d']]) {
  const bytes=Array.from(new Uint8Array(base.add(r).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('cargo admission entry differs');
 }
 hook(0x5ae130,{onEnter(a){this.h=active?this.context.ecx:null;if(this.h&&!mine(this.h.add(0x30).readPointer()))this.h=null;if(this.h){this.size=a[0].toInt32();this.used=this.h.add(0x74).readU32();}},onLeave(ret){if(this.h)emit('cargo-admit',{size:this.size,occupied:this.used,result:ret.toInt32()});}});
 hook(0x5806f0,{onEnter(){this.h=active?this.context.ecx:null;if(this.h&&!mine(this.h.add(0x30).readPointer()))this.h=null;},onLeave(ret){if(this.h)emit('cargo-free',{occupied:this.h.add(0x74).readU32(),free:ret.toInt32()});}});
 const pendingCount=h=>{let n=0;for(let i=0;i<10;i++){const p=pair(h.add(0x100+i*12));if(p[0]!==0xffffffff||p[1]!==0xffffffff)n++;}return n;};
 for(const [r,prefix] of [[0x5a34f0,'558bec51837d0800894dfc750833c08be55dc208'],[0x5a0800,'558bec53568bf1578b7e3085ff7507e8ac54abff'],[0x5aa8a0,'5333db56578db1000100008d7b0a8bff8b460423']]) {
  const bytes=Array.from(new Uint8Array(base.add(r).readByteArray(20))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('boarding entry differs');
 }
 for(const [r,name] of [[0x5a34f0,'board-reserve'],[0x5a0800,'board-release']])hook(r,{onEnter(a){this.h=active?this.context.ecx:null;if(this.h){if(r===0x5a34f0&&!a[0].isNull())boardProbeUnit=a[0];this.target=a[0].isNull()?null:pair(a[0].add(12));this.before=pendingCount(this.h);}},onLeave(ret){if(this.h)emit(name,{target:this.target,before:this.before,after:pendingCount(this.h),result:ret.toInt32()});}});
 hook(0x5aa8a0,{onEnter(){this.h=active?this.context.ecx:null;this.caller=this.returnAddress.sub(base).toString();},onLeave(ret){if(this.h)emit('board-space',{count:pendingCount(this.h),space:ret.toInt32(),caller:this.caller});}});
 hook(0x58edd0,{onEnter(a){this.h=active?this.context.ecx:null;if(this.h&&!mine(this.h.add(0x30).readPointer()))this.h=null;if(this.h){this.target=a[0];this.ground=a[1];this.water=a[2];this.force=a[3].toUInt32();}},onLeave(ret){if(this.h)emit('shore-approach',{unit:unit(this.h.add(0x30).readPointer()),target:unit(this.target),ground:pair(this.ground),water:pair(this.water),force:this.force,result:ret.toInt32()});}});
 hook(0x692120,{onEnter(a){if(active&&mine(this.context.ecx))emit('point-task-create',{unit:unit(this.context.ecx),code:a[0].toUInt32(),x:a[1].isNull()?null:a[1].readU32(),y:a[2].isNull()?null:a[2].readU32(),range:a[3].isNull()?null:a[3].readU32(),caller:this.returnAddress.sub(base).toString()});}});
 hook(0x059ca0,{onEnter(a){this.keep=active;if(this.keep)emit('shore-query',{source:pair(this.context.ecx),target:pair(this.context.edx),masks:Array.from({length:7},(_,i)=>a[i+2].toUInt32())});}});
 emit('module' ,{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
