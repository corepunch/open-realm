// Read-only public corpse timer and animation-scale cancellation witness.
let installed=false,active=false,base,tick=0,seq=0;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mine=u=>!u.isNull()&&u.add(0x30).readU32()===0x68666f6f;
const unit=u=>({identity:pair(u.add(12)),flags:u.add(0x5c).readU32(),scaleFlags:u.add(0x280).readU32(),head:pair(u.add(0x174)),scaleWords:[u.add(0x2f8).readU32(),u.add(0x2fc).readU32(),u.add(0x300).readU32()]});
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('O343 '))return;if(value.includes('label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);emit('marker',{value});if(value.includes('label=complete'))active=false;}});
 for(const [r,name] of [[0x67e130,'cancel'],[0x695480,'restore-scale'],[0x69c000,'flesh'],[0x69c110,'bones'],[0x690490,'completion'],[0x5fa7a0,'arrival'],[0x603110,'cant-path']]){
  hook(r,{onEnter(){if(!active)return;let u=this.context.ecx;if(r===0x5fa7a0||r===0x603110)u=u.add(0x30).readPointer();if(!mine(u))return;this.u=u;emit(name+'-enter',{unit:unit(u),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u)});}});
 }
 hook(0x219060,{onEnter(a){if(active)emit('suspend-decay',{handle:a[0].toUInt32(),suspend:a[1].toUInt32()});}});
 hook(0x69c620,{onEnter(a){if(!active||!mine(this.context.ecx))return;this.u=this.context.ecx;emit('wait-enter',{unit:unit(this.u),delay:a[0].readU32(),scale:a[1].toUInt32(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.u)emit('wait-leave',{unit:unit(this.u)});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
