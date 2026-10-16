// Read-only entry hooks. Correlate nested publishers without process addresses.
let installed=false,recording=true,active=false,serial=0;
const counts={},frames=[];
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Base191 PE mismatch');
 if(installed)return;installed=true;
 Interceptor.attach(base.add(0x231df0),{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  active=true;emit('marker',{value});if(value.includes(' label=complete'))active=false;
 }});
 const current=()=>frames.length?frames[frames.length-1]:null;
 for(const [rva,event]of [[0x206f00,'jass'],[0x5ffb60,'point-task'],[0x9d44d0,'captain']]){
  Interceptor.attach(base.add(rva),{onEnter(args){
   this.observe=active;if(!this.observe)return;
   const data={id:++serial,parent:current()?.id||0,caller:this.returnAddress.sub(base).toUInt32()};
   if(event==='jass'){data.order=args[3].toUInt32();data.point=[args[4].readU32(),args[5].readU32()];}
   if(event==='captain'){data.point=[args[0].readU32(),args[1].readU32()];data.range=args[3].readU32();data.retain=args[2].toUInt32();data.prepare=args[4].toUInt32();}
   this.frame={event,...data,owner:this.context.ecx};frames.push(this.frame);emit(event+'-begin',data);
  },onLeave(result){if(this.observe){if(frames.pop()!==this.frame)throw new Error('publisher nesting differs');emit(event+'-end',{id:this.frame.id,result:event==='jass'?result.toUInt32():null});}}});
 }
 Interceptor.attach(base.add(0x05b970),{onEnter(args){
  this.observe=active;if(!this.observe)return;this.frame={event:'bridge',id:++serial,parent:current()?.id||0};
  const parent=current();const data={...this.frame,producer:parent?.event||null,caller:this.returnAddress.sub(base).toUInt32(),point:[args[0].readU32(),args[1].readU32()],events:[args[2].toUInt32(),args[3].toUInt32()],speed:args[5].readU32(),flag:args[6].toUInt32(),range:args[7].readU32()};
  if(parent?.event==='captain')data.actorBridge=this.context.ecx.equals(parent.owner.add(0x44));
  frames.push(this.frame);emit('bridge-begin',data);
 },onLeave(){if(this.observe){if(frames.pop()!==this.frame)throw new Error('bridge nesting differs');emit('bridge-end',{id:this.frame.id});}}});
 Interceptor.attach(base.add(0x1710a0),{onEnter(args){
  if(!active||current()?.event!=='bridge')return;
  emit('range-publish',{bridge:current().id,value:args[0].readU32(),caller:this.returnAddress.sub(base).toUInt32()});
 }});
 emit('installed',{hooks:6});
}});
