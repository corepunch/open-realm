// Read-only Hero agility movement producer and public native return words.
let installed=false,active=false,tick=0,seq=0,base;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const rva=p=>p.sub(base).toUInt32().toString(16);
 const unit=u=>u.isNull()?null:({identity:[u.add(0xc).readU32(),u.add(0x10).readU32()],rawcode:u.add(0x30).readU32(),flags:u.add(0x5c).readU32()});
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;const match=value.match(/tick=(\d+)/);if(match)tick=Number(match[1]);
  emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 for(const[r,name]of[[0x203a90,'default-speed'],[0x203d30,'current-speed']])
  hook(r,{onEnter(a){if(active)this.row={handle:a[0].toUInt32(),caller:rva(this.returnAddress)};},onLeave(ret){if(this.row)emit(name,{...this.row,result:ret.toUInt32()});}});
 hook(0x528b00,{onEnter(a){if(!active)return;this.out=a[0];const h=this.context.ecx;this.row={hero:h.toString(),base:h.add(0xa8).readS32(),growth:h.add(0xe4).readU32(),cached:h.add(0xb8).readU32(),caller:rva(this.returnAddress),unit:unit(h.add(0x30).readPointer())};},onLeave(){if(this.row)emit('hero-contribution',{...this.row,result:this.out.readU32()});}});
 hook(0x52aad0,{onEnter(){if(!active)return;this.h=this.context.ecx;this.row={hero:this.h.toString(),before:this.h.add(0xb8).readU32(),caller:rva(this.returnAddress)};},onLeave(){if(this.row)emit('hero-refresh',{...this.row,after:this.h.add(0xb8).readU32()});}});
 hook(0x5fb740,{onEnter(a){if(active)emit('move-additive-delta',{delta:a[0].readU32(),before:this.context.ecx.add(0x70).readU32(),caller:rva(this.returnAddress)});}});
 hook(0x05c5c0,{onEnter(a){if(active)emit('speed-publication',{maximum:a[0].readU32(),caller:rva(this.returnAddress)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
