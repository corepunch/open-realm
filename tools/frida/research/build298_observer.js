// Read-only Human work handoff. Canonical unit/queue words; no native calls or writes.
let installed=false,active=false,base,tick=0,scene=0,seq=0;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const isWorker=u=>!u.isNull()&&u.add(0x30).readU32()===0x68573938;
const unit=u=>u.isNull()?null:({identity:pair(u.add(12)),rawcode:u.add(0x30).readU32(),head:pair(u.add(0x19c)),tail:pair(u.add(0x1a8)),count:u.add(0x1b4).readS32(),task:pair(u.add(0x174)),flags:u.add(0x5c).readU32(),depth:u.add(0x198).readS32()});
const order=o=>o.isNull()?null:({identity:pair(o.add(12)),command:o.add(0x24).readU32(),flags:o.add(0x20).readU32(),next:pair(o.add(0x2c))});
Process.attachModuleObserver({onAdded(m){if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c),rel=p=>p.sub(base).toString();
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('B298 '))return;
  if(value.includes(' label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 hook(0x544090,{onEnter(a){if(!active)return;const u=this.context.ecx.add(0x30).readPointer();if(!isWorker(u))return;this.u=u;emit('handoff-enter',{unit:unit(u),structure:unit(a[0]),caller:rel(this.returnAddress)});},onLeave(){if(this.u)emit('handoff-leave',{unit:unit(this.u)});}});
 hook(0x5440a2,{onEnter(){if(!active)return;const u=this.context.edi.add(0x30).readPointer();if(isWorker(u))emit('after-build-task-retire',{unit:unit(u)});}});
 for(const[r,name]of[[0x691c70,'transient'],[0x67abe0,'dispatch'],[0x680320,'admit']])hook(r,{onEnter(a){if(!active||!isWorker(this.context.ecx))return;this.u=this.context.ecx;emit(name+'-enter',{unit:unit(this.u),order:order(a[0]),arg1:a[1].toUInt32(),...(name==='dispatch'?{}:{arg2:a[2].toUInt32()}),caller:rel(this.returnAddress)});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u)});}});
 for(const[r,name]of[[0x409630,'repair-work'],[0x436e10,'repair-stop']])hook(r,{onEnter(){if(!active)return;const u=this.context.ecx.add(0x30).readPointer();if(!isWorker(u))return;this.u=u;emit(name+'-enter',{unit:unit(u),caller:rel(this.returnAddress)});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
