// Bounded read-only observation: no game calls or explicit memory writes.
let installed=false,recording=true,active=false,base,seq=0,next=0;
const counts={},parents=new Map(),scalars=new Map();
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
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32(),rva=p=>p.sub(base).toString();
 const admit=frame=>{if(frame.id===null){frame.id=next++;emit('caller-enter',{id:frame.id,entry:frame.entry,arguments:frame.args,c:counter(),caller:frame.caller});}};
 installed=true;emit('module',{base:base.toString()});
 Interceptor.attach(base.add(0x231df0),{onEnter(a){
  if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;emit('marker',{value,c:counter()});if(value.includes(' label=complete'))active=false;
 }});
 const publicRoots=[0x204c80,0x2103a0,0x215d30];
 const motionRoots=[0x05c0e0,0x16fe20,0x1603d0,0x1606e0,0x16f630,0x169e10,0x16b2f0,0x16cb80,0x171f30,0x91b460];
 for(const entry of [...publicRoots,...motionRoots])Interceptor.attach(base.add(entry),{
  onEnter(a){
   if(!recording)return;this.enabled=true;this.thread=this.threadId;
   const chain=parents.get(this.thread)||[];
   this.frame={id:null,entry,caller:rva(this.returnAddress),args:publicRoots.includes(entry)?[entry===0x204c80?a[0].toUInt32():a[0].readU32()]:[]};
   chain.push(this.frame);parents.set(this.thread,chain);if(active)admit(this.frame);
  },onLeave(ret){if(this.enabled){if(this.frame.id!==null)emit('caller-leave',{id:this.frame.id,result:publicRoots.includes(entry)?ret.toUInt32():null,c:counter()});const chain=parents.get(this.thread);if(chain.pop()!==this.frame)throw Error('caller stack differs');}}
 });
 const binary=[0x06f9c0,0x06fa90,0x06fbb0,0x06fcd0,0x070fe0],reads=[0x070120,0x070170],values=[0x070d80,0x071530,0x06fd30];
 const roots=[...binary,...reads,...values,0x070700,0x070c80,0x070d20,0x0711e0,0x071250,0x071480,0x0715c0,0x071570];
 for(const entry of roots)Interceptor.attach(base.add(entry),{
  onEnter(a){
   const chain=parents.get(this.threadId)||[];if(!recording||!active||!chain.length)return;
   this.enabled=true;this.thread=this.threadId;this.id=next++;const parent=chain.at(-1);admit(parent);
   const stack=scalars.get(this.thread)||[];this.outer=stack.length?stack.at(-1):null;stack.push(this.id);scalars.set(this.thread,stack);
   const value=values.includes(entry),read=reads.includes(entry),integer=entry===0x070d80;
   this.inputs=value?[]:read?[this.context.ecx]:[this.context.edx,...(binary.includes(entry)?[a[0]]:[])];
   this.outputs=read||value&&!integer?[]:[this.context.ecx];
   const pointers=[...this.inputs,...this.outputs];
   emit('scalar-enter',{id:this.id,entry,parent:parent.id,outer:this.outer,caller:rva(this.returnAddress),c:counter(),
    inputs:this.inputs.map(p=>p.readU32()),values:value?[integer?this.context.edx.toUInt32():this.context.ecx.toUInt32()]:[],
    pointers:pointers.map(p=>p.toString()),sp:this.context.esp.toString(),
    equality:pointers.map(p=>pointers.map(q=>p.equals(q)))});
  },onLeave(ret){if(this.enabled){emit('scalar-leave',{id:this.id,c:counter(),result:ret.toUInt32(),inputs:this.inputs.map(p=>p.readU32()),outputs:this.outputs.map(p=>p.readU32())});if(scalars.get(this.thread).pop()!==this.id)throw Error('scalar stack differs');}}
 });
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
