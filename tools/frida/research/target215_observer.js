// Read-only TARGET-03 visibility policy producer and effective consumer flags.
// Entry hooks only; the public markers supply admission outcomes.
let installed=false,active=false,base,seq=0;const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE mismatch');
 installed=true;emit('module',{base:base.toString()});
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const v=a[0].readCString();if(!v||!v.startsWith(config.prefix))return;
  if(v.includes(' label=start'))active=true;emit('marker',{value:v});if(v.includes(' label=complete'))active=false;}});
 hook(0x37a4e0,{onEnter(){if(active)emit('show-map',{caller:rva(this.returnAddress)});}});
 for(const entry of [0x24f930,0x24f9c0])hook(entry,{onEnter(a){if(active)emit('fog-setter',{entry:entry.toString(16),enabled:a[0].toUInt32(),caller:rva(this.returnAddress)});}});
 hook(0x699b20,{onEnter(a){if(active)emit('reveal',{player:a[0].toUInt32(),masks:[this.context.ecx.add(0x148).readU32(),this.context.ecx.add(0x14c).readU32()]});}});
 hook(0x5ff490,{onEnter(){if(active)emit('lost',{ability:this.context.ecx.toString(),caller:rva(this.returnAddress)});}});
 hook(0x1dd920,{onEnter(a){if(active&&rva(this.returnAddress)==='66fe33')emit('world-query',{player:a[0].toUInt32(),flags:a[2].toUInt32(),mode:a[3].toUInt32(),caller:rva(this.context.ebp.add(4).readPointer()),unitFlags:a[1].add(0x5c).readU32()});}});
}
Process.attachModuleObserver({onAdded:install});rpc.exports={finish(){return{counts,readOnly:true};}};
