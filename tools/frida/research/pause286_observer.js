// Read-only real admission, user FIFO and activation. No game calls or writes.
let installed=false,recording=true,window=false,actor=null,base,scene=-1,seq=0,depth=0,counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
function payload(id){
 if(id.every(x=>x===0xffffffff))return ptr(0);
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(0x14)).some((x,i)=>x!==id[i])||p.add(0x20).readU32()?ptr(0):p.add(0x54).readPointer();
}
function order(p){return p.isNull()?null:{identity:pair(p.add(0xc)),flags:p.add(0x20).readU32(),command:p.add(0x24).readU32()};}
function state(u){
 const head=pair(u.add(0x19c)),tail=pair(u.add(0x1a8)),task=pair(u.add(0x174)),t=payload(task);
 return {head,tail,count:u.add(0x1b4).readU32(),head_order:order(payload(head)),tail_order:order(payload(tail)),task,task_code:t.isNull()?null:t.add(0x30).readU32(),flags:u.add(0x5c).readU32(),suppression:u.add(0x198).readS32(),suspension:u.add(0x54).readS32(),ability_disable:u.add(0x1c4).readS32()};
}
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,scene,...data});}
function mine(u){return recording&&window&&actor!==null&&u.equals(actor);}
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const hook=(r,handlers)=>Interceptor.attach(base.add(r),handlers);
 hook(0x231df0,{onEnter(a){if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  scene=Number(/scene=(\d+)/.exec(value)[1]);
  if(value.includes('label=scene-start'))actor=null;
  if(value.includes('label=before'))window=true;
  send({event:'marker',value});if(actor!==null)emit('state',{state:state(actor)});
  if(value.includes('label=settled')||value.includes('label=complete'))window=false;
 }});
 hook(0x697770,{onEnter(){this.take=recording&&window;if(this.take){actor=this.context.ecx;emit('pause-enter',{state:state(actor)});}},onLeave(){if(this.take)emit('pause-exit',{state:state(actor)});}});
 for(const [rva,name]of [[0x69dc80,'validate-immediate'],[0x69dd60,'validate-point'],[0x69df70,'validate-target']])hook(rva,{
  onEnter(a){this.unit=this.context.ecx;this.take=mine(this.unit);if(this.take)emit(name+'-enter',{command:a[0].toUInt32(),state:state(this.unit)});},
  onLeave(v){if(this.take)emit(name+'-exit',{result:v.toUInt32(),state:state(this.unit)});}
 });
 hook(0x48c8c0,{onEnter(a){this.unit=this.context.ecx;this.take=mine(this.unit);if(this.take)emit('disable-enter',{arguments:[0,1,2,3].map(i=>a[i].toUInt32()),caller:this.returnAddress.sub(base).toString(),state:state(this.unit)});},onLeave(){if(this.take)emit('disable-exit',{state:state(this.unit)});}});
 for(const [rva,name]of [[0x680320,'admit'],[0x673e80,'cancel-successors'],[0x693490,'append'],[0x67abe0,'dispatch'],[0x67bd10,'immediate-event'],[0x67c230,'point-event'],[0x67d2b0,'target-event']])hook(rva,{
  onEnter(a){this.unit=this.context.ecx;this.take=mine(this.unit);if(!this.take)return;
   const hasOrder=['admit','append','dispatch','immediate-event','point-event','target-event'].includes(name);
   if(name==='dispatch')depth++;
   emit(name+'-enter',{order:hasOrder?order(a[0]):null,mode:name==='admit'?a[1].toUInt32():null,dispatch:name==='admit'?a[2].toUInt32():name==='dispatch'?a[1].toUInt32():null,state:state(this.unit)});
  },onLeave(){if(!this.take)return;emit(name+'-exit',{state:state(this.unit)});if(name==='dispatch')depth--;}
 });
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,depth,counts};}};
