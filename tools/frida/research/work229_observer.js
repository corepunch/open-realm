// Read-only original mesh inputs/results, owned MAP-02.2 fixture only.
let installed=false, recording=true, groups=0, queries=0;
const emit=(event,data={})=>{if(recording)send({event,...data});};
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;emit('module',{base:base.toString(),path:m.path});
 const at=r=>base.add(r),words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 const active=new Map(),seen=new Set(),decks=new Map();
 Interceptor.attach(at(0x125520),{onEnter(args){
  this.rec=null;if(!decks.has(Process.getCurrentThreadId())||groups>=64||this.returnAddress.sub(base).toUInt32()!==0x18bd89)return;
  const count=args[1].toUInt32(),stride=args[3].toUInt32(),n=args[7].toUInt32();
  if(count>4096||n>16384||stride!==12)throw Error('mesh bounds differ');
  let matrices=1;
  if(!args[4].isNull())for(let i=0;i<count;i++)matrices=Math.max(matrices,1+args[4].add(i*args[5].toUInt32()).readU8());
  if(matrices>256)throw Error('matrix bounds differ');
  this.rec={origin:words(this.context.ecx,3),direction:words(this.context.edx,3),vertices:words(args[2],count*3),count,
   matrices:args[0].isNull()?null:words(args[0],matrices*12),vertexGroups:args[4].isNull()?null:Array.from({length:count},(_,i)=>args[4].add(i*args[5].toUInt32()).readU8()),
   primitive:args[6].toUInt32(),indices:Array.from({length:n},(_,i)=>args[8].add(i*2).readU16())};
  const key=JSON.stringify([this.rec.vertices,this.rec.matrices,this.rec.primitive,this.rec.indices]);
  if(seen.has(key)){this.rec=null;return;}seen.add(key);
  this.out=args[9];this.index=args[10];groups++;active.set(Process.getCurrentThreadId(),this.rec);
 },onLeave(result){if(!this.rec)return;active.delete(Process.getCurrentThreadId());this.rec.hit=result.toInt32();this.rec.distance=this.out.readU32();this.rec.triangle=this.index.readU32();emit('mesh-group',this.rec);}});
 let spheres=0;Interceptor.attach(at(0x18ca19),function(){if(decks.has(Process.getCurrentThreadId())&&spheres++<8)emit('sphere',{bits:words(this.context.ebp.sub(16),4)});});
 Interceptor.attach(at(0x125678),function(){const r=active.get(Process.getCurrentThreadId());if(r)r.transformed=words(at(0xd4addc).readPointer(),r.count*3);});
 Interceptor.attach(at(0x782a80),{onEnter(args){decks.set(Process.getCurrentThreadId(),true);this.out=args[1];this.point=words(args[0],2);this.take=queries++<3000;},onLeave(result){decks.delete(Process.getCurrentThreadId());if(this.take)emit('deck',{point:this.point,hit:result.toInt32(),height:this.out.readU32()});}});
 Interceptor.attach(at(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(value.startsWith(config.prefix))emit('marker',{value});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {groups,queries};}};
