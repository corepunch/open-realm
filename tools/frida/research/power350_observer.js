// Read-only bounded public Pow lifetime observer; no game calls or writes.
let installed=false,recording=true,base,seq=0,active=false,tid=0;
const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,...data});}
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base; const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 for(const [r,prefix] of Object.entries(config.prologues)) {
  const bytes=Array.from(new Uint8Array(base.add(Number(r)).readByteArray(prefix.length/2))).map(x=>x.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('entry bytes differ '+r);
 }
 if(!base.add(0x231df1).readPointer().equals(base.add(0xd68a24)))throw Error('Preload relocated operand differs');
 installed=true;emit('module',{base:base.toString()});
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const rva=p=>p.sub(base).toString();
 Interceptor.attach(base.add(0x231df0),{onEnter(a){
  if(!recording||a[0].isNull())return;const value=a[0].readCString();
  if(value.startsWith(config.prefix))emit('marker',{value,c:counter()});
 }});
 Interceptor.attach(base.add(0x20f990),{onEnter(a){
  if(!recording)return;active=true;tid=this.threadId;
  emit('power-enter',{base:a[0].readU32(),power:a[1].readU32(),c:counter(),tid,
   stack:Thread.backtrace(this.context,Backtracer.ACCURATE).map(rva)});
 },onLeave(ret){if(recording)emit('power-leave',{result:ret.toUInt32(),c:counter()});active=false;}});
 Interceptor.attach(base.add(0x071180),{onEnter(a){if(recording&&active)emit('integer-power-enter',{exponent:a[0].toInt32(),c:counter()});}});
 for(const [r,name]of [[0x070c20,'exp'],[0x06fe10,'exp-quarter']])
  Interceptor.attach(base.add(r),{onEnter(){if(recording&&active)emit('reduction-enter',{name,input:this.context.edx.readU32(),c:counter(),caller:rva(this.returnAddress)});}});
 setInterval(()=>{if(!recording||!active)return;
  const thread=Process.enumerateThreads().find(t=>t.id===tid);
  emit('lifetime-sample',{c:counter(),tid,state:thread?.state,pc:thread?.context?rva(thread.context.pc):null});
 },250);
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
