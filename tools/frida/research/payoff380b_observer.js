// Read-only Chemical Rage380 observer revision B. Concrete reverse4c8650 is a one-byte RET
// and cannot be intercepted safely; its complete static guard is retained. Common551f70
// plus form-enable hooks observe the actual inverse; no synthetic reverse event is emitted.
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
  if(![0x4564656d,0x45646d6d,0x4e616c63,0x4e616c6d,0x4e616c32,0x4e616c33,0x4e74696e,0x4e726f62,0x756f6273,0x75627370,0x75676172,0x7567726d,0x75637279,0x7563726d,0x6f73706d,0x6f737077,0x68666f6f,0x65646f63,0x65646f74,0x6564636d,0x6564746d,0x65777370,0x65676f6c,0x4870616c,0x6e6d7972,0x6e6d7973,0x6e6e7267,0x6e6e7273,0x6e736e70,0x6e736273].includes(code))return null;
  const bridge=u.add(0x164),slot=bridge.add(8).readU32(),generation=bridge.add(12).readU32();
  // Canonical identity is recorded, never resolved by calling the game.
  return {identity:[u.add(0xc).readU32(),u.add(0x10).readU32()],rawcode:code,
   work:u.add(0x20).readU32(),flags:u.add(0x5c).readU32(),pauseDepth:u.add(0x54).readS32(),
   counter158:u.add(0x158).readS32(),invisibilityDepth:u.add(0x114).readS32(),invisibilityScalar:Array.from({length:6},(_,i)=>u.add(0x118+i*4).readU32()),disableDepth:u.add(0x198).readS32(),moverIdentity:[slot,generation]};
 };
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const match=value.match(/tick=(\d+)/);if(match)tick=Number(match[1]);
  emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 for(const[r,name,ability]of[[0x6696e0,'invisibility-acquire',false],[0x694b20,'invisibility-release',false],[0x48b7f0,'counter158-acquire',false],[0x48ec80,'counter158-release',false],[0x48ef40,'begin-work',false],[0x48bca0,'end-work',false],
  [0x688d90,'acquire',false],[0x6785c0,'release',false],[0x693d50,'refresh',false],
  [0x435f10,'teleport-begin',true],[0x40be30,'teleport-end',true],[0x4eecd0,'avatar-begin',true],[0x4cde10,'avatar-end',true],[0x569e20,'morph-begin',true],[0x551f70,'morph-end',true],[0x4eebb0,'channel-begin',true],[0x4cc780,'channel-end',true],[0x9bd6e0,'ai-scope',false],[0x544140,'construction-begin',true],[0x566dd0,'construction-end',true],
  [0x69c5c0,'suppress-begin',false],[0x69c5e0,'suppress-end',false]])
  hook(r,{onEnter(){if(!active)return;this.u=ability?this.context.ecx.add(0x30).readPointer():this.context.ecx;
   const before=unit(this.u);if(!before)return;this.row={before,caller:rva(this.returnAddress)};
   emit(name+'-enter',this.row);},onLeave(){if(this.row)emit(name+'-leave',{after:unit(this.u),caller:this.row.caller});}});
 hook(0x05c9c0,{onEnter(a){if(!active)return;const u=this.context.ecx.sub(0x164),state=unit(u);if(!state)return;
  emit('configuration',{state,args:[a[0].toInt32(),a[1].toUInt32()&255,a[2].toUInt32()&255,a[3].toUInt32()&255]});}});
 hook(0x0608d0,{onEnter(a){if(!active)return;const event=a[1].toUInt32();if(event===0xd01c4||event===0xd01db)emit(event===0xd01c4?'transition-timer':'form-lifetime-timer',{delay:a[0].readU32(),caller:rva(this.returnAddress)});}});
 hook(0x4ba940,{onEnter(a){if(!active)return;this.u=this.context.ecx;const state=unit(this.u);if(state)emit('submerge-category',{state,hide:this.context.edx.toUInt32(),keepBuild:a[0].toUInt32()});}});
 hook(0x4ba8d0,{onEnter(a){if(!active)return;const state=unit(this.context.ecx);if(state)emit('burrow-category',{state,hide:this.context.edx.toUInt32()});}});

 const availability=a=>({class:rva(a.readPointer()),word50:a.add(0x50).readU32(),flags:a.add(0x20).readU32(),execution:a.add(0x3c).readS32(),orders:a.add(0x40).readS32()});
 for(const [address,name]of [[0x4076e0,'ability-disable-add'],[0x40bb00,'ability-disable-remove'],[0x54eb80,'morph-form-disable'],[0x54cb50,'morph-form-enable']])hook(address,{onEnter(a){if(!active)return;const object=this.context.ecx,klass=rva(object.readPointer());if(!['b67448','ae0b24'].includes(klass))return;this.a=object;this.name=name;emit(name+'-enter',{availability:availability(object),state:unit(object.add(0x30).readPointer()),args:name.startsWith('ability-disable')?[a[0].toInt32(),a[1].toInt32()]:[]});},onLeave(){if(this.name)emit(this.name+'-leave',{availability:availability(this.a),state:unit(this.a.add(0x30).readPointer())});}});
 for(const [address,name]of [[0x4c84c0,'chemical-forward-callback'],[0x540e30,'chemical-detach']])hook(address,{onEnter(){if(!active)return;const object=this.context.ecx;if(rva(object.readPointer())!=='ae0b24')return;this.a=object;emit(name+'-enter',{availability:availability(object),state:unit(object.add(0x30).readPointer()),caller:rva(this.returnAddress)});},onLeave(){if(this.a)emit(name+'-leave',{availability:availability(this.a),state:unit(this.a.add(0x30).readPointer())});}});
 // Complete guarded common policy function; record the actual returned word.
 hook(0x54c640,{onEnter(){if(!active)return;const object=this.context.ecx,klass=rva(object.readPointer());
  if(!['ae0b24'].includes(klass))return;this.a=object;
  emit('morph-policy-enter',{availability:availability(object),state:unit(object.add(0x30).readPointer())});
 },onLeave(result){if(this.a)emit('morph-policy-leave',{availability:availability(this.a),state:unit(this.a.add(0x30).readPointer()),result:result.toUInt32()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
