// Read-only entry hooks for retained-head/current-task TargetLost reissue.
let base,installed=false,active=false,seq=0;const counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,...data});};
const resolve=(id,offset)=>{
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(offset)).some((x,i)=>x!==id[i])?ptr(0):p;
};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 hook(0x5ff490,{onEnter(a){if(!active)return;const u=this.context.ecx.add(0x30).readPointer();
  const payload=id=>{const w=resolve(id,0x14);return w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();};
  const h=payload(pair(u.add(0x19c))),taskId=pair(u.add(0x174)),t=payload(taskId);
  const target=a[0].add(0xc).readPointer();
  emit('lost',{caller:rva(this.returnAddress),head:h.isNull()?null:{command:h.add(0x24).readU32(),point:[h.add(0x48).readU32(),h.add(0x50).readU32()]},
   task:t.isNull()?null:{code:t.add(0x30).readU32()},
   targetFlags:target.isNull()?null:[target.add(0x20).readU32(),target.add(0x5c).readU32()]});}});
 hook(0x69bd80,{onEnter(a){if(active&&rva(this.returnAddress)==='5ff6eb')emit('replacement',{caller:rva(this.returnAddress),command:this.context.ecx.toUInt32(),player:this.context.edx.toUInt32(),target:a[1].isNull()?null:a[1].add(0x30).readU32(),point:[a[2].readU32(),a[3].readU32()],tail:[a[0].toUInt32(),a[4].toUInt32(),a[5].toUInt32()]});}});
 hook(0x1dd920,{onEnter(a){if(active&&rva(this.returnAddress)==='66fe33')emit('world-query',{flags:a[2].toUInt32(),caller:rva(this.context.ebp.add(4).readPointer()),unitFlags:a[1].add(0x5c).readU32()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
