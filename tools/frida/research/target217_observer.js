// Entry-only visibility/reveal observer; no game calls or memory writes.
let base,installed=false,active=false,seq=0,target=null;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const masks=p=>[p.add(0x148).readU32(),p.add(0x14c).readU32()];
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x68b780,{onEnter(){if(active)target=this.context.ecx;}});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;
  emit('marker',{value});
  if(target&&!value.includes(' label=complete'))emit('reveal-state',{masks:masks(target),flags:[target.add(0x20).readU32(),target.add(0x5c).readU32()]});
  if(value.includes(' label=complete'))active=false;
 }});
 hook(0x699540,{onEnter(a){if(!active)return;target=this.context.ecx;
  emit('share',{caller:rva(this.returnAddress),player:a[0].toUInt32(),enabled:a[1].toUInt32(),before:masks(target)});}});
 hook(0x66fdd0,{onEnter(a){if(!active)return;
  emit('query',{caller:rva(this.returnAddress),flags:a[1].toUInt32(),mode:a[2].toUInt32(),masks:masks(a[0]),targetFlags:a[0].add(0x5c).readU32()});}});
 hook(0x699b20,{onEnter(a){if(active)emit('fallback',{caller:rva(this.returnAddress),player:a[0].toUInt32(),masks:masks(this.context.ecx)});}});
 hook(0x651010,{onEnter(a){if(active)emit('lost',{caller:rva(this.returnAddress),masks:masks(this.context.ecx)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
