// Read-only unit-owned Undead work request, its deadline and delivery.
let installed=false,active=false,base,tick=0,scene=0,seq=0;const counts={},controls=new Map();
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mine=u=>!u.isNull()&&u.add(0x30).readU32()===0x75573936;
const unit=u=>({identity:pair(u.add(12)),head:pair(u.add(0x19c)),flags:u.add(0x5c).readU32(),suppression:u.add(0x198).readS32(),receiver:u.readPointer().add(12).readPointer().sub(base).toString()});
const clock=p=>({time:p.add(0x40).readU32(),epoch:p.add(0x44).readU32(),span:p.add(0x48).readU32()});
const current=()=>clock(base.add(0xd53a48).readPointer().add(0x14));
const request=p=>({deadline:p.add(4).readU32(),delay:p.add(8).readU32(),flags:p.add(16).readU32(),serial:p.add(20).readU32(),value:p.add(28).readU32(),clock:clock(p.add(12).readPointer())});
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('B296 '))return;
  if(value.includes(' label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);
  emit('marker',{value,clock:current()});if(value.includes(' label=complete'))active=false;}});
 hook(0x544350,{onEnter(){if(!active)return;const u=this.context.ecx.add(0x30).readPointer();if(!mine(u))return;this.u=u;emit('work-enter',{unit:unit(u),clock:current()});},onLeave(){if(this.u)emit('work-leave',{unit:unit(this.u),clock:current()});}});
 hook(0x69c620,{onEnter(a){if(!active||!mine(this.context.ecx))return;this.u=this.context.ecx;controls.set(this.u.add(0x180).toString(),this.u);emit('wait-enter',{unit:unit(this.u),delay:a[0].readU32(),scale:a[1].toUInt32(),clock:current(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.u)emit('wait-leave',{unit:unit(this.u),clock:current()});}});
 hook(0x0608d0,{onEnter(a){this.u=active?controls.get(this.context.ecx.toString()):null;if(!this.u||a[1].toUInt32()!==0xd019d)return;emit('request',{unit:unit(this.u),delay:a[0].readU32(),code:a[1].toUInt32(),clock:current()});}});
 hook(0x0542d0,{onEnter(){const p=this.context.ecx;this.u=active?controls.get(p.add(24).readPointer().toString()):null;if(!this.u)return;emit('execute-enter',{unit:unit(this.u),request:request(p),clock:current()});},onLeave(){if(this.u)emit('execute-leave',{unit:unit(this.u),clock:current()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
