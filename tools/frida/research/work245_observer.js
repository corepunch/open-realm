// Read-only public Attack facing requests, predicted inputs and physical commits.
let installed=false,active=false,tick=0,scene=0,seq=0;const counts={};
const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;const base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>p.sub(base).toUInt32().toString(16);
 const clock=()=>{const o=base.add(0xd53a48).readPointer();return{c:o.add(0x538).readU32(),clock:words(o.add(0x54),3)};};
 const mover=p=>p.isNull()?null:({id:words(p.add(0x14),2),clock:words(p.add(0x70),2),pose:words(p.add(0x78),6),range:p.add(0xb0).readU32(),parameters:words(p.add(0xb4),3),flags:p.add(0xd8).readU32()});
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const v=a[0].readCString();if(!v||!v.startsWith(config.prefix))return;
  if(v.includes(' label=start'))active=true;const hit=/tick=(\d+) scene=(\d+)/.exec(v);if(hit){tick=Number(hit[1]);scene=Number(hit[2]);}
  emit('marker',{value:v,...clock()});if(v.includes(' label=complete'))active=false;}});
 hook(0x49a240,{onEnter(a){if(active)emit('chase',{...clock(),caller:rva(this.returnAddress),turn_only:a[1].toUInt32()});}});
 hook(0x05a5c0,{onEnter(a){if(active)emit('target-request',{...clock(),range:a[1].readU32(),persistent:a[5].toUInt32(),warp:a[6].toUInt32()});}});
 hook(0x15f660,{onEnter(a){this.take=active;if(!this.take)return;this.row={...clock(),self:mover(this.context.ecx),target:words(a[0],2),half_angle:a[1].readU32()};},onLeave(r){if(this.take)emit('facing-test',{...this.row,result:r.toUInt32()});}});
 hook(0x16bcf0,{onEnter(){if(active)emit('publish',{...clock(),flags:this.context.ecx.add(0x100).readU32()});}});
 hook(0x16c150,{onEnter(){if(!active)return;const g=this.context.ecx,n=g.add(0x38).readU32(),p=g.add(0x28).readPointer();if(n>12)throw Error('member count');emit('group',{...clock(),flags:g.add(0x80).readU32(),members:Array.from({length:n},(_,i)=>mover(p.add(i*44+0x14).readPointer()))});}});
 hook(0x16fe20,{onEnter(a){this.take=active;if(!this.take)return;this.p=this.context.ecx;this.row={...clock(),before:mover(this.p),speed:a[0].readU32(),heading:a[1].readU32()};},onLeave(){if(this.take)emit('motion',{...this.row,after:mover(this.p)});}});
 hook(0x49c440,{onEnter(){if(active)emit('swing',{...clock(),caller:rva(this.returnAddress)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,installed,readOnly:true};}};
