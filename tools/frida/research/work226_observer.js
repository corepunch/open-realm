// Read-only repair/construction separation configuration and suppression owners.
let installed=false,active=false,tick=0,seq=0,base;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const rva=p=>p.compare(base)>=0&&p.compare(base.add(config.imageSize))<0?p.sub(base).toUInt32().toString(16):'interceptor-tail-transfer';
 const unit=u=>{
  if(u.isNull())return null;
  const code=u.add(0x30).readU32();
  if(code!==0x68573236&&code!==0x684e3236)return null;
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
  [0x409630,'repair-begin',true],[0x436e10,'repair-end',true]])
  hook(r,{onEnter(){if(!active)return;this.u=ability?this.context.ecx.add(0x30).readPointer():this.context.ecx;
   const before=unit(this.u);if(!before)return;this.row={before,caller:rva(this.returnAddress)};
   emit(name+'-enter',this.row);},onLeave(){if(this.row)emit(name+'-leave',{after:unit(this.u),caller:this.row.caller});}});
 hook(0x05c9c0,{onEnter(a){if(!active)return;const u=this.context.ecx.sub(0x164),state=unit(u);if(!state)return;
  emit('configuration',{state,args:[a[0].toInt32(),a[1].toUInt32()&255,a[2].toUInt32()&255,a[3].toUInt32()&255]});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
