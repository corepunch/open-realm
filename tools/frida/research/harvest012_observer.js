// ORDER-01.12 read-only observer: public markers plus user-order admission/append
// for Peasant/Peon/Ghoul/Acolyte/Wisp workers. No game calls and no writes.
let installed=false,active=false,base,tick=0,scene=0,seq=0;
const counts={};
const workers=new Set([0x68706561,0x6f70656f,0x7567686f,0x7561636f,0x65777370]);
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const worker=u=>!u.isNull()&&workers.has(u.add(0x30).readU32());
const unit=u=>u.isNull()?null:({identity:pair(u.add(12)),rawcode:u.add(0x30).readU32(),user:pair(u.add(0x19c)),tail:pair(u.add(0x1a8)),count:u.add(0x1b4).readU32(),internal:pair(u.add(0x174)),flags:u.add(0x5c).readU32()});
const order=o=>o.isNull()?null:({identity:pair(o.add(12)),command:o.add(0x24).readU32(),flags:o.add(0x20).readU32()});
const clock=()=>{const p=base.add(0xd53a48).readPointer().add(0x14);return{time:p.add(0x40).readU32(),epoch:p.add(0x44).readU32(),span:p.add(0x48).readU32()};};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!/^[A-Z]012 /.test(value))return;
  if(value.includes(' label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);
  emit('marker',{value,clock:clock()});if(value.includes(' label=complete'))active=false;}});
 hook(0x680320,{onEnter(a){if(!active)return;const u=this.context.ecx;if(!worker(u))return;this.u=u;
  emit('admit-enter',{unit:unit(u),order:order(a[0]),mode:a[1].toInt32(),dispatch:a[2].toInt32(),caller:this.returnAddress.sub(base).toString()});},
  onLeave(){if(this.u)emit('admit-leave',{unit:unit(this.u)});}});
 hook(0x693490,{onEnter(a){if(!active)return;const u=this.context.ecx;if(!worker(u))return;this.u=u;
  emit('append-enter',{unit:unit(u),order:order(a[0]),caller:this.returnAddress.sub(base).toString()});},
  onLeave(){if(this.u)emit('append-leave',{unit:unit(this.u)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
