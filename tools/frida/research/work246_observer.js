// Read-only fine-coordinate target sampling and destination publication.
let installed=false,active=false,tick=0,seq=0;const counts={};
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;const base=m.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const clock=()=>{const o=base.add(0xd53a48).readPointer();return{c:o.add(0x538).readU32(),clock:words(o.add(0x54),3)};};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const hit=/tick=(\d+)/.exec(value);if(hit)tick=Number(hit[1]);
  emit('marker',{value,...clock()});if(value.includes(' label=complete'))active=false;}});
 hook(0x16cd30,{onEnter(a){this.take=active;if(!this.take)return;this.g=this.context.ecx;this.o=a[0];this.row={...clock(),g:this.g.toString(),cd:this.g.add(0x64).readS32(),target:words(this.g.add(0x40)),offset:words(this.g.add(0x4c))};},
  onLeave(){if(this.take)emit('sample',{...this.row,dest:words(this.o)});}});
 hook(0x16ce10,{onEnter(a){this.take=active;if(!this.take)return;this.g=this.context.ecx;this.row={...clock(),g:this.g.toString(),input:words(a[0])};},
  onLeave(r){if(this.take)emit('route',{...this.row,result:r.toUInt32(),dest:words(this.g.add(0x3c).readPointer().add(0x1c))});}});
 hook(0x168b80,{onEnter(a){if(active)emit('setdest',{...clock(),path:this.context.ecx.toString(),dest:words(a[0])});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,installed,readOnly:true};}};
