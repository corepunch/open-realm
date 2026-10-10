// Read-only owner transition and Move subscription delivery. No game calls.
let base,installed=false,active=false,seq=0,scene=-1,tick=0;const counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,scene,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const unit=u=>({identity:pair(u.add(0xc)),owner:u.add(0x58).readU32(),head:pair(u.add(0x19c)),task:pair(u.add(0x174))});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;
  const match=value.match(/tick=(\d+) scene=(\d+)/);if(match){tick=Number(match[1]);scene=Number(match[2]);}
  emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 hook(0x698ce0,{onEnter(a){if(!active)return;this.u=this.context.ecx;
  emit('owner-enter',{unit:unit(this.u),next:a[0].toUInt32(),caller:rva(this.returnAddress)});
 },onLeave(){if(this.u)emit('owner-leave',{unit:unit(this.u)});}});
 hook(0x5fdfd0,{onEnter(a){if(!active)return;this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();
  emit('owner-handler-enter',{unit:unit(this.u),target:pair(this.a.add(0xcc)),caller:rva(this.returnAddress)});
 },onLeave(){if(this.a)emit('owner-handler-leave',{unit:unit(this.u),target:pair(this.a.add(0xcc))});}});
 for(const [r,name]of [[0x5fbea0,'clear-target'],[0x5fb190,'recover'],[0x5fa7a0,'arrival']])
  hook(r,{onEnter(){if(active)emit(name,{unit:unit(this.context.ecx.add(0x30).readPointer()),caller:rva(this.returnAddress)});}});
 hook(0x651010,{onEnter(){if(active)emit('target-lost',{unit:unit(this.context.ecx),caller:rva(this.returnAddress)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
