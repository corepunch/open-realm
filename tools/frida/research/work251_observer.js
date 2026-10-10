// Read-only native admission and task-factory observation. No game calls.
let base,installed=false,active=false,scene=0,tick=0;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,scene,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;
  tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);emit('marker',{value});
  if(value.includes(' label=complete'))active=false;
 }});
 hook(0x2071ea,{onEnter(){if(active)emit('admission',{result:this.context.eax.toUInt32(),order:this.context.esi.toUInt32(),flags:this.context.ebp.sub(4).readU32()});}});
 hook(0x69bd80,{onEnter(a){if(active&&this.returnAddress.sub(base).toUInt32()===0x2072e3)emit('target-order',{order:this.context.ecx.toUInt32(),target:a[1].isNull()?null:[a[1].add(0x20).readU32(),a[1].add(0x5c).readU32()],point:[a[2].readU32(),a[3].readU32()]});}});
 hook(0x5fd270,{onEnter(){if(active)emit('factory');}});
 hook(0x66fdd0,{onEnter(a){if(active)emit('query',{caller:this.returnAddress.sub(base).toUInt32(),flags:a[1].toUInt32(),mode:a[2].toUInt32()});}});
}});rpc.exports={finish(){return{installed,readOnly:true,counts};}};
