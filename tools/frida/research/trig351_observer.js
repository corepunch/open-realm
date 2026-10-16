// Read-only scalar operand and nested caller observations; no native calls/writes.
let installed=false,recording=true,active=false,base,seq=0,next=0;
const counts={},parents=new Map();
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,...data});}
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 for(const[r,bytes]of Object.entries(config.prologues)){
  const actual=Array.from(new Uint8Array(base.add(Number(r)).readByteArray(bytes.length/2))).map(x=>x.toString(16).padStart(2,'0')).join('');
  if(actual!==bytes)throw Error('entry differs '+r);
 }
 if(!base.add(0x231df1).readPointer().equals(base.add(0xd68a24)))throw Error('Preload relocation differs');
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const rva=p=>p.sub(base).toString();installed=true;emit('module');
 Interceptor.attach(base.add(0x231df0),{onEnter(a){
  if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;
  emit('marker',{value,c:counter()});
  if(value.includes(' label=complete'))active=false;
 }});
 const publicRoots=[0x1f75d0,0x1f8250,0x1f8290,0x1f8310,0x1f9580,0x215d00,0x216750];
 const motionRoots=[0x05c0e0,0x16fe20,0x169e10,0x16b2f0,0x16cb80,0x1d4c80,0x171f30];
 for(const entry of [...publicRoots,...motionRoots])Interceptor.attach(base.add(entry),{
  onEnter(a){
   if(!recording||!active)return;this.capture=true;this.id=next++;this.thread=this.threadId;
   let chain=parents.get(this.thread)||[];this.previous=chain.slice();chain.push({id:this.id,entry});parents.set(this.thread,chain);
   const args=publicRoots.includes(entry)?[a[0].readU32(),...(entry===0x1f8290?[a[1].readU32()]:[])]:[];
   emit('caller-enter',{id:this.id,entry,arguments:args,c:counter(),caller:rva(this.returnAddress)});
  },onLeave(ret){if(this.capture){emit('caller-leave',{id:this.id,result:publicRoots.includes(entry)?ret.toUInt32():null,c:counter()});parents.set(this.thread,this.previous);}}
 });
 for(const entry of [0x06ffa0,0x0703a0,0x070530,0x0705b0,0x070790,0x071280,0x071340,0x071590])Interceptor.attach(base.add(entry),{
  onEnter(a){
   const chain=parents.get(this.threadId)||[];
   if(!recording||!active||!chain.length)return;
   this.capture=true;this.id=next++;
   const paired=entry===0x071340;
   this.inputs=paired?[this.context.ecx]:[this.context.edx,...(entry===0x070530?[a[0]]:[])];
   this.outputs=paired?[this.context.edx,a[0]]:[this.context.ecx];
   emit('scalar-enter',{id:this.id,entry,parent:chain.at(-1).id,caller:rva(this.returnAddress),c:counter(),
    inputs:this.inputs.map(p=>p.readU32()),
    equality:[...this.inputs,...this.outputs].map(p=>[...this.inputs,...this.outputs].map(q=>p.equals(q))),
    stack_offsets:[...this.inputs,...this.outputs].map(p=>p.sub(this.context.esp).toInt32())});
  },onLeave(ret){if(this.capture)emit('scalar-leave',{id:this.id,c:counter(),inputs:this.inputs.map(p=>p.readU32()),outputs:this.outputs.map(p=>p.readU32())});}
 });
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
