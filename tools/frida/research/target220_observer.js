// Read-only user queue and Move task boundaries. No native calls or writes.
let base,installed=false,active=false,seq=0,mover=null,scene=-1;const counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,scene,...data});};
const resolve=(id,offset)=>{
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(offset)).some((x,i)=>x!==id[i])?ptr(0):p;
};
const payload=id=>{const w=resolve(id,0x14);return w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();};
const order=p=>{if(p.isNull())return null;const command=p.add(0x24).readU32();return {identity:pair(p.add(0xc)),command,flags:p.add(0x20).readU32(),...(command===851986?{point:[p.add(0x48).readU32(),p.add(0x50).readU32()],target:pair(p.add(0x58))}:{})};};
const state=u=>{
 const head=payload(pair(u.add(0x19c))),tail=payload(pair(u.add(0x1a8))),task=payload(pair(u.add(0x174)));
 return {unit:pair(u.add(0xc)),flags:u.add(0x5c).readU32(),count:u.add(0x1b4).readU32(),head:order(head),tail:order(tail),task:task.isNull()?null:{code:task.add(0x30).readU32()}};
};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x693490,{onEnter(a){if(!active)return;this.unit=this.context.ecx;
  if(this.unit.add(0x58).readU32()>=12){this.unit=null;return;}mover=this.unit;
  emit('append-enter',{order:order(a[0]),state:state(this.unit),caller:rva(this.returnAddress)});
 },onLeave(){if(this.unit)emit('append-leave',{state:state(this.unit)});}});
 hook(0x67abe0,{onEnter(a){if(active)emit('dispatch-head',{order:order(a[0]),state:state(this.context.ecx),dispatch:a[1].toUInt32(),caller:rva(this.returnAddress)});}});
 hook(0x692120,{onEnter(a){if(active)emit('point-task',{code:a[0].toUInt32(),unit:pair(this.context.ecx.add(0xc)),point:[a[1].readU32(),a[2].readU32()],caller:rva(this.returnAddress)});}});
 hook(0x6926b0,{onEnter(a){if(active)emit('target-task',{code:a[0].toUInt32(),unit:pair(this.context.ecx.add(0xc)),target:a[1].isNull()?null:pair(a[1].add(0xc)),caller:rva(this.returnAddress)});}});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const match=value.match(/scene=(\d+)/);if(match)scene=Number(match[1]);
  if(value.includes(' label=begin'))mover=null;emit('marker',{value});
  if(mover&&!value.includes(' label=complete'))emit('order-state',state(mover));
  if(value.includes(' label=complete'))active=false;
 }});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
