// Read-only parameter publications, temporary group overrides and native getters.
let installed=false,active=false,finishing=false,base,tick=0,seq=0;const counts={},query=[0,0,0,0,0,0];
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;installed=true;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 for(const [r,prefix]of Object.entries(config.prologues)){
  const bytes=Array.from(new Uint8Array(base.add(Number(r)).readByteArray(prefix.length/2))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('entry bytes differ '+r);
 }
 if(!base.add(0x231df1).readPointer().equals(base.add(0xd68a24)))throw Error('Preload relocated operand differs');
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const mover=m=>({id:words(m.add(0x14)),pos:words(m.add(0x78)),vel:words(m.add(0x80)),clock:words(m.add(0x70)),parameters:[0x88,0xb0,0xb4,0xb8,0xbc,0xc0,0xc4].map(o=>m.add(o).readU32()),heading:m.add(0x8c).readU32(),group:words(m.add(0x9c))});
 const group=g=>{const count=g.add(0x38).readU32(),data=g.add(0x28).readPointer();if(count>12)throw Error('physical count');return{id:words(g.add(0x14)),count,flags:g.add(0x80).readU32(),turn:g.add(0x74).readU32(),members:Array.from({length:count},(_,i)=>{const r=data.add(i*0x2c),m=r.add(0x14).readPointer();return{id:words(r),requested:r.add(0x20).readU32(),heading:r.add(0x24).readU32(),m:m.isNull()?null:mover(m)};})};};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);if(value.includes('label=start'))active=true;const m=/label=unit u=(\d+)/.exec(value);if(m)emit('public-values',{u:Number(m[1]),values:query.slice()});emit('marker',{value,c:counter()});if(value.includes('label=complete'))finishing=true;if(finishing&&m&&Number(m[1])===7)active=false;}});
 for(const [r,k]of [[0x204100,0],[0x204140,1],[0x203b80,2],[0x203d30,3],[0x204090,4],[0x203df0,5]])hook(r,{onEnter(){this.take=active;},onLeave(ret){if(this.take)query[k]=ret.toUInt32();}});
 for(const [r,field]of [[0x15ff40,'maximum'],[0x170f60,'increment'],[0x170f80,'turn'],[0x171200,'window']])hook(r,{onEnter(a){this.take=active;this.m=this.context.ecx;if(this.take)emit('write-enter',{field,c:counter(),caller:this.returnAddress.sub(base).toString(),value:a[0].readU32(),m:mover(this.m)});},onLeave(){if(this.take)emit('write-leave',{field,c:counter(),m:mover(this.m)});}});
 for(const [r,name]of [[0x05b970,'point'],[0x05a5c0,'target'],[0x05c0e0,'facing']])hook(r,{onEnter(){this.take=active;if(this.take)emit('request-enter',{kind:name,c:counter(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.take)emit('request-leave',{kind:name,c:counter()});}});
 hook(0x16c570,{onEnter(){this.take=active;this.g=this.context.ecx;if(this.take)emit('commit-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('commit-leave',{c:counter(),g:group(this.g)});}});
 for(const [r,name]of [[0x2181a0,'add'],[0x218d40,'remove']])hook(r,{onEnter(a){this.take=active;this.code=a[1].toUInt32();},onLeave(ret){if(this.take)emit('ability-result',{kind:name,code:this.code,result:ret.toUInt32()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{installed,counts,readOnly:true};}};
