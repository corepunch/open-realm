// Read-only original Build admission and task/current-head publication.
let installed=false,active=false,seq=0,tick=0,base;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const rel=p=>p.compare(base)>=0&&p.compare(base.add(config.imageSize))<0?p.sub(base).toUInt32().toString(16):'outside';
 const unit=u=>{if(u.isNull())return null;const code=u.add(0x30).readU32();
  if(![0x68573935,0x6f573935,0x75573935,0x65573935,0x6e573935].includes(code))return null;
  return{rawcode:code,identity:[u.add(12).readU32(),u.add(16).readU32()],head:[u.add(0x19c).readU32(),u.add(0x1a0).readU32()],work:u.add(0x20).readU32(),depth:u.add(0x198).readS32()};};
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('B295 '))return;
  if(value.includes(' label=start'))active=true;const match=value.match(/tick=(\d+)/);if(match)tick=Number(match[1]);
  emit('marker',{value});if(value.includes(' label=complete'))active=false;}});
 hook(0x206a20,{onEnter(a){if(!active)return;this.row={command:a[1].toUInt32(),point:[a[2].readU32(),a[3].readU32()]};emit('build-native-enter',this.row);},onLeave(ret){if(this.row)emit('build-native-leave',{...this.row,accepted:ret.toInt32()});}});
 hook(0x680320,{onEnter(a){if(!active)return;const u=unit(this.context.ecx);if(!u)return;
  this.u=this.context.ecx;this.row={unit:u,command:a[0].add(0x24).readU32(),mode:a[1].toInt32(),dispatch:a[2].toInt32(),caller:rel(this.returnAddress)};emit('admit-enter',this.row);},onLeave(){if(this.row)emit('admit-leave',{unit:unit(this.u),command:this.row.command});}});
 for(const[r,name]of[[0x544140,'inside-work'],[0x566dd0,'inside-end'],[0x409630,'repair-work'],[0x544090,'human-repair']])
  hook(r,{onEnter(){if(!active)return;const u=this.context.ecx.add(0x30).readPointer(),state=unit(u);if(!state)return;this.u=u;emit(name+'-enter',{unit:state,caller:rel(this.returnAddress)});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
