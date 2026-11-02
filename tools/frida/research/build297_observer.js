// Read-only structure-completion callback, retained identity and generated order.
let installed=false,active=false,base,tick=0,scene=0,seq=0,completing=null;
const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mine=u=>!u.isNull()&&u.add(0x30).readU32()===0x75573937;
const unit=u=>u.isNull()?null:({identity:pair(u.add(12)),rawcode:u.add(0x30).readU32(),head:pair(u.add(0x19c)),flags:u.add(0x5c).readU32(),suppression:u.add(0x198).readS32()});
const clock=()=>{const p=base.add(0xd53a48).readPointer().add(0x14);return{time:p.add(0x40).readU32(),epoch:p.add(0x44).readU32(),span:p.add(0x48).readU32()};};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('B297 '))return;
  if(value.includes(' label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);
  emit('marker',{value,clock:clock()});if(value.includes(' label=complete'))active=false;}});
 hook(0x544350,{onEnter(a){if(!active)return;const ability=this.context.ecx,u=ability.add(0x30).readPointer();if(!mine(u))return;this.u=u;this.a=ability;emit('begin-enter',{unit:unit(u),structure:unit(a[0]),overlayParent:unit(a[2]),retained:pair(ability.add(0xb0)),clock:clock()});},onLeave(){if(this.u)emit('begin-leave',{unit:unit(this.u),retained:pair(this.a.add(0xb0)),clock:clock()});}});
 hook(0x543f00,{onEnter(a){if(!active)return;const ability=this.context.ecx,u=ability.add(0x30).readPointer();if(!mine(u))return;this.u=u;this.a=ability;this.previous=completing;completing=u;emit('complete-enter',{unit:unit(u),structure:unit(a[0]),retained:pair(ability.add(0xb0)),clock:clock()});},onLeave(){if(this.u){emit('complete-leave',{unit:unit(this.u),retained:pair(this.a.add(0xb0)),clock:clock()});completing=this.previous;}}});
 hook(0x543e00,{onEnter(a){if(!active)return;const ability=this.context.ecx,u=ability.add(0x30).readPointer();if(!mine(u))return;this.u=u;this.a=ability;emit('parent-retire-enter',{unit:unit(u),structure:unit(a[0]),retained:pair(ability.add(0xb0)),clock:clock()});},onLeave(){if(this.u)emit('parent-retire-leave',{unit:unit(this.u),retained:pair(this.a.add(0xb0)),clock:clock()});}});
 hook(0x69bd80,{onEnter(a){if(!active||!completing||this.context.ecx.toUInt32()!==0xd0032)return;emit('completion-order-create',{unit:unit(completing),code:this.context.ecx.toUInt32(),target:unit(a[1]),caller:this.returnAddress.sub(base).toString(),clock:clock()});}});
 hook(0x680320,{onEnter(){if(active&&completing&&this.context.ecx.equals(completing))emit('completion-order-admit',{unit:unit(completing),clock:clock()});}});
 for(const r of [0x5447d0,0x546660,0x544fe0])hook(r,{onEnter(a){if(active){this.yes=true;emit('validation-enter',{rva:r,args:[a[0].toString(),a[1].toString()]});}},onLeave(ret){if(this.yes)emit('validation-leave',{rva:r,result:ret.toInt32()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
