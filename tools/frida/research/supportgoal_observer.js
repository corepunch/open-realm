// Read-only FOOT-04.2 water/bridge route goals: request, retry result, physical binding and commits.
// No NativeFunction, game invocation, replacement, or memory write.
let installed=false,recording=true,active=false,base,scene=-1,tick=0,seq=0;const counts={};
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,scene,tick,...data});}
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:module.path});
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
 hook(0x165ae0,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;this.dest=a[1];if(this.take)emit('advance-enter',{c:counter(),point:words(a[0]),source:words(a[0]),requested:words(a[1]),goal:words(this.p.add(0x1c)),flags:this.p.add(0x88).readU32(),path:path(this.p)});},onLeave(ret){if(this.take)emit('advance-leave',{c:counter(),result:ret.toUInt32(),point:words(this.dest),index:this.p.add(0x74).readS32(),flags:this.p.add(0x88).readU32(),path:path(this.p)});}});
 hook(0x166e90,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;if(this.take)emit('fine-enter',{c:counter(),source:words(a[0]),requested:words(a[1]),goal:words(this.p.add(0x1c)),flags:this.p.add(0x88).readU32(),path:path(this.p)});},onLeave(ret){if(this.take)emit('fine-leave',{c:counter(),result:ret.toUInt32(),index:this.p.add(0x74).readS32(),flags:this.p.add(0x88).readU32(),path:path(this.p),points:Array.from({length:this.p.add(0x50).readU32()},(_,i)=>words(this.p.add(0x40).readPointer().add(i*8)))});}});
 hook(0x170080,{onEnter(){this.take=recording&&active;this.m=this.context.ecx;if(this.take)emit('recover-enter',{c:counter(),m:mover(this.m)});},onLeave(ret){if(this.take)emit('recover-leave',{c:counter(),result:ret.toUInt32(),m:mover(this.m)});}});
 hook(0x170fa0,{onEnter(a){if(!recording||!active||a[0].isNull())return;emit('bind',{c:counter(),m:mover(this.context.ecx),g:group(a[0])});}});
 hook(0x16c570,{onEnter(){this.take=recording&&active;this.g=this.context.ecx;if(this.take)emit('commit-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('commit-leave',{c:counter(),g:group(this.g)});}});

 hook(0x167290,{onEnter(a){this.take=recording&&active;this.p=this.context.ecx;if(this.take)emit('retry-enter',{c:counter(),source:words(a[0]),retries:this.p.add(0x98).readU32(),path:path(this.p)});},onLeave(ret){if(this.take)emit('retry-leave',{c:counter(),result:ret.toUInt32(),retries:this.p.add(0x98).readU32(),path:path(this.p)});}});
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
