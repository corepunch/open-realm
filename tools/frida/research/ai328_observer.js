// Read-only inside-construction separation configuration and suppression owners.
let installed=false,active=false,tick=0,seq=0,base;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,primary:event==='module'?null:Array.from({length:3},(_,i)=>base.add(0xd53a48).readPointer().add(0x54+i*4).readU32()),...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 Process.setExceptionHandler(d=>{send({event:'native-exception',type:d.type,address:d.address.toString(),rva:d.address.sub(base).toString(),memory:d.memory,trace:Thread.backtrace(d.context,Backtracer.ACCURATE).map(p=>p.sub(base).toString())});return false;});
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const rva=p=>p.compare(base)>=0&&p.compare(base.add(config.imageSize))<0?p.sub(base).toUInt32().toString(16):'interceptor-tail-transfer';
 const unit=u=>{
  if(u.isNull())return null;
  const code=u.add(0x30).readU32();
  if(![0x68666f6f,0x65646f63,0x65646f74,0x6564636d,0x6564746d,0x65777370,0x65676f6c,0x4870616c].includes(code))return null;
  const bridge=u.add(0x164),slot=bridge.add(8).readU32(),generation=bridge.add(12).readU32();
  // Canonical identity is recorded, never resolved by calling the game.
  return {identity:[u.add(0xc).readU32(),u.add(0x10).readU32()],rawcode:code,
   work:u.add(0x20).readU32(),flags:u.add(0x5c).readU32(),pauseDepth:u.add(0x54).readS32(),
   disableDepth:u.add(0x198).readS32(),moverIdentity:[slot,generation]};
 };
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const match=value.match(/tick=(\d+)/);if(match)tick=Number(match[1]);
  emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 for(const[r,name,ability]of[[0x48ef40,'begin-work',false],[0x48bca0,'end-work',false],
  [0x688d90,'acquire',false],[0x6785c0,'release',false],[0x693d50,'refresh',false],
  [0x435f10,'teleport-begin',true],[0x40be30,'teleport-end',true],[0x4eecd0,'avatar-begin',true],[0x4cde10,'avatar-end',true],[0x569e20,'morph-begin',true],[0x551f70,'morph-end',true],[0x4eebb0,'channel-begin',true],[0x4cc780,'channel-end',true],[0x9bd6e0,'ai-scope',false],[0x544140,'construction-begin',true],[0x566dd0,'construction-end',true],
  [0x69c5c0,'suppress-begin',false],[0x69c5e0,'suppress-end',false]])
  hook(r,{onEnter(){if(!active)return;this.u=ability?this.context.ecx.add(0x30).readPointer():this.context.ecx;
   const before=unit(this.u);if(!before)return;this.row={before,caller:rva(this.returnAddress)};
   if(name==='ai-scope')emit('ai-producer-stack',{state:before,trace:Thread.backtrace(this.context,Backtracer.ACCURATE).map(rva)});
   emit(name+'-enter',this.row);},onLeave(){if(this.row)emit(name+'-leave',{after:unit(this.u),caller:this.row.caller});}});
 hook(0x05c9c0,{onEnter(a){if(!active)return;const u=this.context.ecx.sub(0x164),state=unit(u);if(!state)return;
  emit('configuration',{state,args:[a[0].toInt32(),a[1].toUInt32()&255,a[2].toUInt32()&255,a[3].toUInt32()&255]});}});
 hook(0x0608d0,{onEnter(a){if(active&&a[1].toUInt32()===0xd01c4)emit('transition-timer',{delay:a[0].readU32(),caller:rva(this.returnAddress)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
