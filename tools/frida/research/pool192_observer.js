// Observe public burst construction/growth/zero-reference reclamation; no writes.
let installed=false,recording=true,active=false,serial=0;
const counts={},payloads=new Map(),wrappers=new Map(),frames=[];
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts,livePayloads:payloads.size,liveWrappers:wrappers.size};}};
Process.attachModuleObserver({onAdded(module){
 if(module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Pool192 PE mismatch');
 if(installed)return;installed=true;
 Interceptor.attach(base.add(0x231df0),{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();if(!value?.startsWith(config.prefix))return;
  active=true;emit('marker',{value});if(value.includes(' label=complete'))active=false;
 }});
 for(const [rva,kind,factory]of [[0x6807a0,'order',0xd70e14],[0x680db0,'task',0xd70f1c]]){
  Interceptor.attach(base.add(rva),{onEnter(){this.observe=active;},onLeave(result){
   if(!this.observe)return;
   const key=result.toString();if(payloads.has(key))throw new Error('live payload reused');
   const id=++serial;payloads.set(key,{id,kind});
   emit('construct',{id,kind,refs:result.add(4).readU32(),identity:[result.add(12).readU32(),result.add(16).readU32()],poolLive:base.add(factory+12).readU32()});
  }});
 }
 Interceptor.attach(base.add(0x06a320),{onEnter(args){
  this.observe=false;if(!active)return;
  const pool=this.context.ecx,size=pool.readU32(),block=pool.add(4).readU32();
  const host=base.add(0xd3c82c).readPointer(),wrapperPool=host.isNull()?ptr(0):host.add(0x34).readPointer();
  const kind=pool.equals(base.add(0xd70e18))?'order':pool.equals(base.add(0xd70f20))?'task':pool.equals(wrapperPool)?'wrapper':null;
  if(!kind)return;
  this.observe=true;this.frame={kind,size,block,grow:pool.add(16).readPointer().isNull()};
  frames.push(this.frame);
 },onLeave(){if(this.observe){if(frames.pop()!==this.frame)throw new Error('pool frame mismatch');}}});
 Interceptor.attach(base.add(0x07c6d2),{onEnter(args){
  if(active&&frames.length)emit('growth',{...frames[frames.length-1],bytes:args[0].toUInt32(),flags:args[3].toUInt32()});
 }});
 Interceptor.attach(base.add(0x057c30),{onEnter(args){
  if(!active||args[0].isNull())return;
  const value=payloads.get(args[0].toString());if(!value)return;
  wrappers.set(this.context.ecx.toString(),value);emit('bind',value);
 }});
 for(const [rva,kind]of [[0x678db0,'order'],[0x678ff0,'task']]){
  Interceptor.attach(base.add(rva),{onEnter(args){
   const key=args[0].toString(),value=payloads.get(key);if(!value)return;
   if(value.kind!==kind)throw new Error('factory reclaim class mismatch');
   emit('reclaim',{...value,refs:args[0].add(4).readU32(),identity:[args[0].add(12).readU32(),args[0].add(16).readU32()]});payloads.delete(key);
  }});
 }
 Interceptor.attach(base.add(0x0576b0),{onEnter(){
  const key=this.context.ecx.toString(),value=wrappers.get(key);if(!value)return;
  emit('wrapper-return',{...value,identity:[this.context.ecx.add(20).readU32(),this.context.ecx.add(24).readU32()],ownedNull:this.context.ecx.add(84).readPointer().isNull()});wrappers.delete(key);
 }});
 emit('installed',{hooks:9});
}});
