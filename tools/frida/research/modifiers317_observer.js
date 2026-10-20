// Read-only nonfinite speed and movement public producer boundaries.
// No NativeFunction, game invocation, replacement, or memory write.
let installed=false,recording=true,active=false,base,scene=-1,tick=0,seq=0;const counts={};
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,scene,tick,counter:base.add(0xd53a48).readPointer().add(0x538).readU32(),primary:words(base.add(0xd53a48).readPointer().add(0x54),3),...data});}
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:module.path});
 Process.setExceptionHandler(d=>{if(d.type==='access-violation')send({event:'native-exception',type:d.type,address:d.address.toString(),rva:d.address.sub(base).toString(),memory:d.memory,context:Object.fromEntries(['eax','ebx','ecx','edx','esi','edi','esp','ebp','eip'].map(k=>[k,d.context[k]?.toString()])),backtrace:Thread.backtrace(d.context,Backtracer.ACCURATE).map(p=>p.sub(base).toString())});return false;});
 const hook=(r,c)=>Interceptor.attach(base.add(r),c),rva=p=>p.sub(base).toString();
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const unit=u=>({id:words(u.add(0xc)),code:u.add(0x30).readU32(),head:words(u.add(0x19c)),task:words(u.add(0x174))});
 const mover=m=>({id:words(m.add(0x14)),pos:words(m.add(0x78)),vel:words(m.add(0x80)),clock:words(m.add(0x70)),maximum:m.add(0x88).readU32(),heading:m.add(0x8c).readU32(),radius:m.add(0x90).readU32(),range:m.add(0xb0).readU32(),flags:m.add(0xd8).readU32(),group:words(m.add(0x9c))});
 const path=p=>({id:p.toString(),goal:words(p.add(0x1c)),index:p.add(0x74).readS32(),count:p.add(0x50).readU32(),flags:p.add(0x88).readU32(),delay:p.add(0x94).readU32(),request:p.add(0x7c).readU32(),target:p.add(0xa4).readPointer().toString()});
 const group=g=>{const count=g.add(0x38).readU32(),data=g.add(0x28).readPointer(),p=g.add(0x3c).readPointer(),members=[];
  if(count>12)throw Error('unexpected native physical size');
  for(let i=0;i<count;i++){const row=data.add(i*0x2c),m=row.add(0x14).readPointer();members.push({id:words(row),flags:row.add(0x28).readU32(),requested:row.add(0x20).readU32(),turn:row.add(0x24).readU32(),m:m.isNull()?null:mover(m)});}
  return {id:words(g.add(0x14)),flags:g.add(0x80).readU32(),target:words(g.add(0x40)),count,members,
   path:p.isNull()?null:{dest:words(p.add(0x1c)),flags:p.add(0x88).readU32(),target:p.add(0xa4).readPointer().toString()}};};
 hook(0x231df0,{onEnter(a){if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  const m=value.match(/tick=(\d+) scene=(-?\d+)/);if(!m)throw Error('marker context');tick=Number(m[1]);scene=Number(m[2]);
  if(value.includes('label=begin-setup'))active=true;
  emit('marker',{value,c:counter(),clock:words(base.add(0xd53a48).readPointer().add(0x54),3)});if(value.includes('label=end-cleanup')||value.includes('label=complete'))active=false;
 }});
 const speedOwner=m=>({unit:m.add(0x30).readPointer().toString(),base:m.add(0x70).readU32(),factor:m.add(0x78).readU32()});
 hook(0x5fb7c0,{onEnter(a){this.take=recording&&active;this.m=this.context.ecx;if(this.take)emit('factor-enter',{delta:a[0].readU32(),owner:speedOwner(this.m),caller:rva(this.returnAddress)});},onLeave(){if(this.take)emit('factor-leave',{owner:speedOwner(this.m)});}});
 hook(0x2154e0,{onEnter(a){this.take=recording&&active;if(this.take)emit('setter-enter',{value:a[1].readU32()});},onLeave(){if(this.take)emit('setter-leave');}});
 hook(0x5fc900,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;this.out=a[0];if(this.take)emit('speed-enter',{owner:speedOwner(this.p)});},onLeave(){if(this.take)emit('speed-leave',{owner:speedOwner(this.p),value:this.out.readU32()});}});
 hook(0x203d30,{onEnter(){this.take=recording&&active;},onLeave(ret){if(this.take)emit('public-speed',{value:ret.toUInt32()});}});
 hook(0x203a90,{onEnter(){this.take=recording&&active;},onLeave(ret){if(this.take)emit('public-default-speed',{value:ret.toUInt32()});}});
 for(const [addr,axis]of [[0x204100,0],[0x204140,1]])hook(addr,{onEnter(){this.take=recording&&active;},onLeave(ret){if(this.take)emit('public-coordinate',{axis,value:ret.toUInt32()});}});
 hook(0x203bf0,{onEnter(){this.take=recording&&active;},onLeave(ret){if(this.take)emit('public-height',{value:ret.toUInt32()});}});
 hook(0x203a60,{onEnter(){this.take=recording&&active;},onLeave(ret){if(this.take)emit('public-default-height',{value:ret.toUInt32()});}});
 hook(0x6742f0,{onEnter(a){this.take=recording&&active;this.out=a[0];this.u=this.context.ecx;},onLeave(){if(this.take)emit('authored-radius',{u:unit(this.u),radius:this.out.readU32()});}});
 hook(0x15fef0,{onEnter(a){if(recording&&active)emit('physical-radius',{radius:a[0].readU32(),m:mover(this.context.ecx)});}});
 hook(0x165ae0,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;this.dest=a[1];if(this.take)emit('advance-enter',{c:counter(),point:words(a[0]),source:words(a[0]),requested:words(a[1]),goal:words(this.p.add(0x1c)),flags:this.p.add(0x88).readU32(),path:path(this.p)});},onLeave(ret){if(this.take)emit('advance-leave',{c:counter(),result:ret.toUInt32(),point:words(this.dest),index:this.p.add(0x74).readS32(),flags:this.p.add(0x88).readU32(),path:path(this.p)});}});
 hook(0x166e90,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;if(this.take)emit('fine-enter',{c:counter(),source:words(a[0]),requested:words(a[1]),goal:words(this.p.add(0x1c)),flags:this.p.add(0x88).readU32(),path:path(this.p)});},onLeave(ret){if(this.take)emit('fine-leave',{c:counter(),result:ret.toUInt32(),index:this.p.add(0x74).readS32(),flags:this.p.add(0x88).readU32(),path:path(this.p),points:Array.from({length:this.p.add(0x50).readU32()},(_,i)=>words(this.p.add(0x40).readPointer().add(i*8)))});}});
 hook(0x170080,{onEnter(){this.take=recording&&active;this.m=this.context.ecx;if(this.take)emit('recover-enter',{c:counter(),m:mover(this.m)});},onLeave(ret){if(this.take)emit('recover-leave',{c:counter(),result:ret.toUInt32(),m:mover(this.m)});}});
 hook(0x170fa0,{onEnter(a){if(!recording||!active||a[0].isNull())return;emit('bind',{c:counter(),m:mover(this.context.ecx),g:group(a[0])});}});
 hook(0x16c570,{onEnter(){this.take=recording&&active;this.g=this.context.ecx;if(this.take)emit('commit-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('commit-leave',{c:counter(),g:group(this.g)});}});

 hook(0x15f7b0,{onEnter(a){this.take=recording&&active;this.m=this.context.ecx;if(this.take)emit('delta-enter',{c:counter(),m:mover(this.m),delta:words(a[0]),notify:a[1].toUInt32()});},onLeave(){if(this.take)emit('delta-leave',{c:counter(),m:mover(this.m)});}});
 hook(0x6515f0,{onEnter(a){this.take=recording&&active;this.x=a[0];this.y=a[1];if(this.take)emit('admit-enter',{c:counter(),point:[this.x.readU32(),this.y.readU32()],query:a[2].toUInt32()});},onLeave(ret){if(this.take)emit('admit-leave',{c:counter(),point:[this.x.readU32(),this.y.readU32()],result:ret.toUInt32()});}});
 hook(0x16ecc0,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;if(this.take)emit('placement-enter',{c:counter(),point:words(this.p),radius:a[0].readU32(),mask:a[1].readU32(),lane:a[2].toUInt32(),filter:a[3].toUInt32(),budget:a[4].toUInt32(),limit:a[5].toUInt32(),scan:a[8].toUInt32(),integer:a[9].toUInt32()});},onLeave(ret){if(this.take)emit('placement-leave',{c:counter(),point:words(this.p),result:ret.toUInt32()});}});
 hook(0x653510,{onEnter(a){this.take=recording&&active;this.x=a[0];this.y=a[1];if(this.take)emit('position-enter',{c:counter(),point:[this.x.readU32(),this.y.readU32()],admission:a[2].toUInt32()});},onLeave(){if(this.take)emit('position-leave',{c:counter(),point:[this.x.readU32(),this.y.readU32()]});}});
 hook(0x04c330,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;if(this.take)emit('clamp-enter',{c:counter(),point:words(this.p)});},onLeave(){if(this.take)emit('clamp-leave',{c:counter(),point:words(this.p)});}});
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
