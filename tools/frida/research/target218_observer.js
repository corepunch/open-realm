// Entry-only detection and target visibility observer; no game calls or writes.
let base,installed=false,active=false,seq=0,target=null,mover=null;const counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,...data});};
const resolve=(id,offset)=>{
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(offset)).some((x,i)=>x!==id[i])?ptr(0):p;
};
const state=p=>{
 const w=resolve(pair(p.add(0x130)),0x14);
 const d=w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();
 return {owner:p.add(0x58).readU32(),unitFlags:p.add(0x5c).readU32(),reveal:[p.add(0x148).readU32(),p.add(0x14c).readU32()],
  masks:d.isNull()?[0,0]:[d.add(0x24).readU32(),d.add(0x6c).readU32()],
  direct:d.isNull()?Array(16).fill(0):Array.from({length:16},(_,i)=>d.add(0x2c+i*4).readU32()),
  second:d.isNull()?Array(16).fill(0):Array.from({length:16},(_,i)=>d.add(0x74+i*4).readU32())};
};
const payload=id=>{const w=resolve(id,0x14);return w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();};
const orderState=u=>{
 const head=payload(pair(u.add(0x19c))),task=payload(pair(u.add(0x174)));
 return {head:head.isNull()?null:{command:head.add(0x24).readU32(),point:[head.add(0x48).readU32(),head.add(0x50).readU32()],target:pair(head.add(0x58))},task:task.isNull()?null:{code:task.add(0x30).readU32()}};
};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x2071ea,{onEnter(){if(active)emit('admission-result',{result:this.context.eax.toUInt32(),order:this.context.esi.toUInt32(),flags:this.context.ebp.sub(4).readU32()});}});
 hook(0x6910c0,{onEnter(a){if(active&&rva(this.returnAddress)==='20723b')emit('point-fallback',{order:this.context.ecx.toUInt32(),player:this.context.edx.toUInt32(),point:[a[1].readU32(),a[2].readU32()]});}});
 hook(0x69bd80,{onEnter(a){if(active&&rva(this.returnAddress)==='2072e3')emit('target-order',{order:this.context.ecx.toUInt32(),player:this.context.edx.toUInt32(),target:a[1].isNull()?null:pair(a[1].add(0xc)),point:[a[2].readU32(),a[3].readU32()]});}});
 hook(0x68bae0,{onEnter(a){if(!active)return;target=this.context.ecx;
  emit('detected-query',{caller:rva(this.returnAddress),player:a[0].toUInt32(),selector:a[1].toUInt32(),...state(target)});}});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;emit('marker',{value});
  if(mover&&!value.includes(' label=begin')&&!value.includes(' label=complete')&&!value.includes(' label=start'))emit('order-state',orderState(mover));
  if(value.includes(' label=complete'))active=false;
 }});
 hook(0x66fdd0,{onEnter(a){if(active&&rva(this.returnAddress)==='5fbc09')mover=this.context.ecx;if(active)emit('query',{caller:rva(this.returnAddress),flags:a[1].toUInt32(),mode:a[2].toUInt32(),...state(a[0])});}});
 hook(0x5fa400,{onEnter(a){if(!active)return;const p=a[0];
  emit('move-damage',{caller:rva(this.returnAddress),flags:p.add(0xc).readU32(),amount:p.add(0x10).readU32()});}});
 for(const offset of [0x5fbc09,0x5fd364,0x5fd4ee])hook(offset,{onEnter(){if(active)emit('query-result',{caller:offset.toString(16),result:this.context.eax.toUInt32()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
