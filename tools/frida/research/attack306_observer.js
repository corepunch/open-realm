// Read-only public Attack request, physical binding, commit and pause lifetimes.
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
 const group=g=>{const count=g.add(0x38).readU32(),data=g.add(0x28).readPointer(),p=g.add(0x3c).readPointer(),members=[];
  if(count>12)throw Error('unexpected native physical size');
  for(let i=0;i<count;i++){const row=data.add(i*0x2c),m=row.add(0x14).readPointer();members.push({id:words(row),flags:row.add(0x28).readU32(),requested:row.add(0x20).readU32(),turn:row.add(0x24).readU32(),m:m.isNull()?null:mover(m)});}
  return {id:words(g.add(0x14)),flags:g.add(0x80).readU32(),target:words(g.add(0x40)),count,members,
   path:p.isNull()?null:{dest:words(p.add(0x1c)),flags:p.add(0x88).readU32(),target:p.add(0xa4).readPointer().toString()}};};
 hook(0x231df0,{onEnter(a){if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  const m=value.match(/tick=(\d+) scene=(-?\d+)/);if(!m)throw Error('marker context');tick=Number(m[1]);scene=Number(m[2]);
  if(value.includes('label=begin-setup'))active=true;
  emit('marker',{value,c:counter()});if(value.includes('label=end-cleanup')||value.includes('label=complete'))active=false;
 }});
 hook(0x49a240,{onEnter(a){if(!recording||!active)return;emit('attack-request',{c:counter(),caller:rva(this.returnAddress),unit:unit(this.context.ecx.add(0x30).readPointer()),turnOnly:a[1].toUInt32()});}});
 hook(0x5fc640,{onEnter(a){if(!recording||!active)return;emit('move-task',{c:counter(),caller:rva(this.returnAddress),unit:unit(this.context.ecx.add(0x30).readPointer()),range:a[1].readU32(),persistent:a[2].toUInt32(),subscribe:a[3].toUInt32()});}});
 hook(0x05a5c0,{onEnter(a){if(recording&&active)emit('target-request',{c:counter(),caller:rva(this.returnAddress),range:a[1].readU32(),persistent:a[5].toUInt32(),targetRegion:a[7].toString()});}});
 hook(0x170fa0,{onEnter(a){if(!recording||!active||a[0].isNull())return;emit('bind',{c:counter(),m:mover(this.context.ecx),g:group(a[0])});}});
 hook(0x16c570,{onEnter(){this.take=recording&&active;this.g=this.context.ecx;if(this.take)emit('commit-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('commit-leave',{c:counter(),g:group(this.g)});}});
 for(const [address,name]of [[0x499310,'arrival'],[0x495180,'try-swing'],[0x5fc120,'cancel-move']])hook(address,{onEnter(){if(recording&&active)emit(name,{c:counter(),caller:rva(this.returnAddress),unit:unit(this.context.ecx.add(0x30).readPointer())});}});
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
