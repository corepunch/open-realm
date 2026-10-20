// Read-only Attack validation, chase requests, recovery and physical groups.
let base,installed=false,active=false,scene=-1,tick=0,seq=0;const counts={};
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,scene,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;base=m.base;installed=true;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const rva=p=>{const v=p.sub(base).toUInt32();return v<config.imageSize?v.toString(16):'external';};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const unit=u=>u.isNull()?null:({identity:words(u.add(0xc)),owner:u.add(0x58).readU32(),head:words(u.add(0x19c)),task:words(u.add(0x174)),state:u.add(0x20).readU32(),reveal:words(u.add(0x148))});
 const attack=a=>({a:a.toString(),unit:unit(a.add(0x30).readPointer()),flags:a.add(0x20).readU32(),target:words(a.add(0x6c))});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const m=value.match(/tick=(\d+) scene=(\d+)/);if(m){tick=Number(m[1]);scene=Number(m[2]);}
  emit('marker',{value,c:counter()});if(value.includes(' label=complete'))active=false;}});
 for(const [r,name]of [[0x49a5f0,'attack-dispatch'],[0x499310,'attack-arrival'],[0x495180,'try-swing'],[0x49a240,'chase'],[0x49b420,'attack-lost'],[0x49d280,'recover'],[0x497e20,'release-target']])
  hook(r,{onEnter(a){if(!active)return;this.a=this.context.ecx;
   emit(name,{c:counter(),caller:rva(this.returnAddress),...attack(this.a),args:[a[0].toString(),a[1].toString()],
    ...(name==='attack-dispatch'?{code:a[0].add(8).readU32()}:{} )});}});
 hook(0x4968e0,{onEnter(a){if(!active)return;this.a=this.context.ecx;this.row={c:counter(),caller:rva(this.returnAddress),...attack(this.a),targetUnit:a[0].isNull()?null:unit(a[0]),visibility:a[1].toUInt32()};},
  onLeave(ret){if(this.row)emit('validate',{...this.row,result:ret.toUInt32()});}});
 hook(0x05a5c0,{onEnter(a){if(active)emit('target-request',{c:counter(),caller:rva(this.returnAddress),self:words(this.context.ecx.add(8)),target:words(a[0].add(8)),range:a[1].readU32(),persistent:a[5].toUInt32()});}});
 hook(0x692120,{onEnter(a){if(active)emit('point-task',{c:counter(),caller:rva(this.returnAddress),unit:unit(this.context.ecx),code:a[0].toUInt32(),point:[a[1].readU32(),a[2].readU32()],range:a[3].readU32()});}});
 hook(0x16c150,{onEnter(){if(!active)return;this.g=this.context.ecx;const p=this.g.add(0x3c).readPointer();
  emit('group',{c:counter(),g:words(this.g.add(0x14)),target:words(this.g.add(0x40)),flags:this.g.add(0x80).readU32(),cd:this.g.add(0x64).readS32(),unseen:this.g.add(0x6c).readU32(),dest:p.isNull()?null:words(p.add(0x1c))});}});
 hook(0x05b580,{onEnter(a){if(!active)return;this.row={c:counter(),caller:rva(this.returnAddress),range:a[0].readU32(),predict:a[2].toUInt32(),source:words(this.context.ecx.add(8)),target:words(a[1].add(8)),constant:base.add(0xd6fb40).readU32()};},onLeave(ret){if(this.row)emit('range-check',{...this.row,result:ret.toUInt32()});}});
 hook(0x4c95c0,{onEnter(){if(active)emit('blink',{c:counter(),ability:this.context.ecx.toString(),point:[this.context.ecx.add(0xf8).readU32(),this.context.ecx.add(0x100).readU32()]});}});
 hook(0x651010,{onEnter(){if(active)emit('target-lost',{c:counter(),unit:unit(this.context.ecx),caller:rva(this.returnAddress)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
