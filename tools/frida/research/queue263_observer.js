// Read-only original user queue, activation, cancellation and deferred release.
// No NativeFunction, game calls, return replacement or memory writes.
let installed=false,recording=true,started=false,tick=0,base,actor=null,seq=0,depth=0;
const tracked=new Map(),pair=p=>[p.readU32(),p.add(4).readU32()];
const key=id=>id.join(':');
const emit=(event,data={})=>send({event,seq:++seq,tick,...data});
function wrapper(id){
 if(id.every(x=>x===0xffffffff))return ptr(0);
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(0x14)).some((x,i)=>x!==id[i])?ptr(0):p;
}
function payload(id){const w=wrapper(id);return w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();}
function order(p){
 if(p.isNull())return null;
 const vtable=p.readPointer().sub(base).toUInt32();
 return {identity:pair(p.add(0xc)),references:p.add(4).readU32(),vtable,
  flags:p.add(0x20).readU32(),command:p.add(0x24).readU32(),
  ...(vtable===0xb787dc?{}:{point:[p.add(0x48).readU32(),p.add(0x50).readU32()],target:pair(p.add(0x58))})};
}
function state(u){
 const head=pair(u.add(0x19c)),tail=pair(u.add(0x1a8)),task=pair(u.add(0x174)),t=payload(task);
 return {user:head,tail,count:u.add(0x1b4).readU32(),tasks:task,
  head_order:order(payload(head)),tail_order:order(payload(tail)),
  task_code:t.isNull()?null:t.add(0x30).readU32(),flags:u.add(0x5c).readU32()};
}
function mine(u){return recording&&started&&actor!==null&&u.equals(actor);}
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;emit('module',{base:base.toString(),path:m.path});
 const hook=(r,callbacks)=>Interceptor.attach(base.add(r),callbacks);
 hook(0x231df0,{onEnter(a){if(a[0].isNull())return;const value=a[0].readCString();if(recording&&value.startsWith(config.prefix)){
  started=true;const hit=/tick=(\d+)/.exec(value);if(hit)tick=Number(hit[1]);emit('marker',{value});
  if(actor!==null)emit('state',{unit:actor.toString(),state:state(actor)});
 }}});
 hook(0x693490,{onEnter(a){this.unit=this.context.ecx;
  if(recording&&started&&actor===null&&a[0].add(0x24).readU32()===851986&&this.unit.add(0x58).readU32()===3)actor=this.unit;
  this.take=mine(this.unit);if(!this.take)return;const o=order(a[0]);tracked.set(key(o.identity),a[0]);
  emit('append',{unit:this.unit.toString(),order:o,state:state(this.unit)});
 },onLeave(){if(this.take)emit('append-end',{unit:this.unit.toString(),state:state(this.unit)});}});
 hook(0x67abe0,{onEnter(a){this.unit=this.context.ecx;this.take=mine(this.unit);if(!this.take)return;
  emit('dispatch',{unit:this.unit.toString(),order:order(a[0]),state:state(this.unit),depth:++depth,dispatch:a[1].toUInt32()});
 },onLeave(){if(this.take)emit('dispatch-end',{unit:this.unit.toString(),state:state(this.unit),depth:depth--});}});
 hook(0x673e80,{onEnter(){this.unit=this.context.ecx;this.take=mine(this.unit);if(this.take)emit('cancel',{unit:this.unit.toString(),state:state(this.unit)});
 },onLeave(){if(this.take)emit('cancel-end',{unit:this.unit.toString(),state:state(this.unit)});}});
 for(const [rva,name]of[[0x67bd10,'immediate-event'],[0x67c230,'point-event'],[0x67d2b0,'target-event']])
  hook(rva,{onEnter(a){if(mine(this.context.ecx))emit(name,{unit:this.context.ecx.toString(),order:order(a[0]),state:state(this.context.ecx)});}});
 hook(0x0557b0,{onEnter(){if(!recording||!started)return;const p=this.context.ecx,id=pair(p.add(0xc));if(tracked.has(key(id)))emit('release-request',{order:order(p)});}});
 hook(0x04c2c0,{onEnter(){if(!recording||!started)return;this.id=pair(this.context.ecx);this.take=tracked.has(key(this.id));
  if(this.take){this.p=tracked.get(key(this.id));emit('release',{identity:this.id,references:this.p.add(4).readU32()});}
 },onLeave(){if(this.take)emit('release-end',{identity:this.id,resolved:!payload(this.id).isNull(),references:this.p.add(4).readU32()});}});
}});
rpc.exports={finish(){recording=false;return{installed,depth,tracked:tracked.size};}};
