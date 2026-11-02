// Read-only BASE-01.1 player input -> synchronized action -> unit admission.
let installed=false, active=false, seq=0, base; const counts={};
function emit(event,data={}) {counts[event]=(counts[event]||0)+1;send({event,seq:++seq,...data});}
function install(m) {
 if(installed || m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4 || pe.add(8).readU32()!==config.timestamp || pe.add(80).readU32()!==config.imageSize)throw Error('PE mismatch');
 installed=true;emit('module',{base:base.toString()});
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const chain=c=>Thread.backtrace(c,Backtracer.ACCURATE).slice(0,20).map(rva);
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 hook(0x231df0,{onEnter(a){const v=a[0].readCString();if(!v || !v.startsWith(config.prefix))return;
  if(v.includes(' label=start'))active=true;emit('marker',{value:v});if(v.includes(' label=complete'))active=false;}});
 for(const entry of [0x6b98f0,0x6b9f70])hook(entry,{onEnter(){if(!active)return;
  const a=this.context.ecx;this.a=a;emit('action',{entry:entry.toString(16),action:a.toString(),words:words(a,16),
   player:a.add(0x15).readU8(),flags:a.add(0x18).readU16(),order:a.add(0x1c).readU32(),point:words(a.add(0x28),2),caller:rva(this.returnAddress),chain:chain(this.context)});
 },onLeave(){if(this.a)emit('action-end',{action:this.a.toString()});}});
 hook(0x6b93a0,{onEnter(a){if(active)emit('publish',{unit:this.context.ecx.toString(),order:this.context.edx.toString(),
  flags:a[0].toUInt32()&0xffff,fallback:a[1].toUInt32(),orderWords:words(this.context.edx,26),caller:rva(this.returnAddress),chain:chain(this.context)});}});
 hook(0x680320,{onEnter(a){if(active)emit('admit',{unit:this.context.ecx.toString(),order:a[0].toString(),mode:a[1].toUInt32(),dispatch:a[2].toUInt32(),
  caller:rva(this.returnAddress),chain:chain(this.context)});}});
 hook(0x6bd5f0,{onEnter(a){if(active)emit('ui-point',{order:this.context.ecx.toUInt32(),source:this.context.edx.toString(),
  point:[a[0].readU32(),a[1].readU32()],target:a[2].toString(),flags:a[3].toUInt32()&0xffff,
  caller:rva(this.returnAddress),chain:chain(this.context)});}});
 hook(0x6b7930,{onEnter(){if(!active)return;const a=this.context.ecx;
  if(a.add(8).readU32()===0xa0012)emit('submit',{action:a.toString(),words:words(a,14),order:a.add(0x1c).readU32(),
   flags:a.add(0x18).readU16(),point:words(a.add(0x28),2),caller:rva(this.returnAddress),chain:chain(this.context)});}});
 hook(0x331b80,{onEnter(){if(!active)return;const a=this.context.edx;
  if(a.add(8).readU32()===0xa0012)emit('serialize',{words:words(a,14),order:a.add(0x1c).readU32(),
   flags:a.add(0x18).readU16(),point:words(a.add(0x28),2),caller:rva(this.returnAddress),chain:chain(this.context)});}});
 hook(0x331850,{onEnter(){this.a=active ? this.context.edx : null;},onLeave(){const a=this.a;
  if(a && a.add(8).readU32()===0xa0012)emit('deserialize',{words:words(a,14),order:a.add(0x1c).readU32(),
   flags:a.add(0x18).readU16(),point:words(a.add(0x28),2),caller:rva(this.returnAddress)});}});
 for(const entry of (config.extra||[]))hook(entry,{onEnter(a){if(active)emit('upstream',{entry:entry.toString(16),ecx:this.context.ecx.toString(),edx:this.context.edx.toString(),args:words(this.context.esp.add(4),8),caller:rva(this.returnAddress),chain:chain(this.context)});}});
}
Process.attachModuleObserver({onAdded:install});rpc.exports={finish(){return{counts,readOnly:true};}};
