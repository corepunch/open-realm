// Read-only shared-radius publication and live canonical mover records.
let installed=false,active=false,finishing=false,base,tick=0,seq=0;const query=[0,0];const counts={};
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
 const mover=m=>({id:words(m.add(0x14)),pos:words(m.add(0x78)),vel:words(m.add(0x80)),clock:words(m.add(0x70)),maximum:m.add(0x88).readU32(),heading:m.add(0x8c).readU32(),radius:m.add(0x90).readU32(),range:m.add(0xb0).readU32(),flags:m.add(0xd8).readU32(),group:words(m.add(0x9c))});
 const shared=p=>p.isNull()?null:{id:words(p.add(0x14)),refs:p.add(0x1c).readU32(),radius:p.add(0x28).readU32()};
 const group=g=>{const count=g.add(0x38).readU32(),data=g.add(0x28).readPointer(),p=g.add(0x3c).readPointer();if(count>12)throw Error('physical count');return{id:words(g.add(0x14)),shared:shared(g.add(0x7c).readPointer()),target:words(g.add(0x40)),count,flags:g.add(0x80).readU32(),cached:p.isNull()?null:p.add(0xb4).readU32(),members:Array.from({length:count},(_,i)=>{const row=data.add(i*0x2c),m=row.add(0x14).readPointer();return{id:words(row),m:m.isNull()?null:mover(m)};})};};
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);if(value.includes('label=start'))active=true;const m=/label=unit u=(\d+)/.exec(value);if(m)emit('public-position',{u:Number(m[1]),xy:query.slice()});emit('marker',{value,c:counter()});if(value.includes('label=complete'))finishing=true;if(finishing&&m&&Number(m[1])===12)active=false;}});
 hook(0x16c220,{onEnter(){this.take=active;this.p=this.context.ecx;this.refs=this.p.add(0x1c).readU32();if(this.take)emit('publish-enter',{c:counter(),s:shared(this.p)});},onLeave(){if(this.take)emit('publish-leave',{c:counter(),s:this.refs?shared(this.p):null});}});
 hook(0x16e1f0,{onEnter(){this.take=active;this.g=this.context.ecx;if(this.take)emit('radius-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('radius-leave',{c:counter(),g:group(this.g)});}});
 hook(0x16c570,{onEnter(){this.take=active;this.g=this.context.ecx;if(this.take)emit('commit-enter',{c:counter(),g:group(this.g)});},onLeave(){if(this.take)emit('commit-leave',{c:counter(),g:group(this.g)});}});
 hook(0x16c940,{onEnter(a){this.take=active;this.g=this.context.ecx;this.out=a[0];},onLeave(){if(this.take)emit('routing-radius',{c:counter(),g:group(this.g),radius:this.out.readU32()});}});
 hook(0x15fef0,{onEnter(a){if(active)emit('set-radius',{c:counter(),m:mover(this.context.ecx),radius:a[0].readU32()});}});
 for(const [r,k]of [[0x204100,0],[0x204140,1]])hook(r,{onEnter(){this.take=active;},onLeave(ret){if(this.take)query[k]=ret.toUInt32();}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{installed,counts,readOnly:true};}};
