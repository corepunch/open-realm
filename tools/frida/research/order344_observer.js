// Read-only can’t-path owner and replacement-order dispatch.
let installed=false,active=false,base,tick=0,scene=0,seq=0;const counts={};
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mine=u=>!u.isNull()&&u.add(0x30).readU32()===0x68666f6f;
const unit=u=>({identity:pair(u.add(12)),flags:u.add(0x5c).readU32(),widgetFlags:u.add(0x20).readU32(),head:pair(u.add(0x19c)),task:pair(u.add(0x174)),ai:pair(u.add(0x304))});
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;installed=true;base=m.base;
 const pe=base.add(base.add(0x3c).readU32());if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith('O344 '))return;if(value.includes('label=start'))active=true;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);emit('marker',{value});if(value.includes('label=complete'))active=false;}});
 for(const [r,name] of [[0x603110,'cant-path'],[0x5fb190,'recover'],[0x5fa7a0,'arrival']]){
  hook(r,{onEnter(){if(!active)return;this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();if(!mine(this.u)){this.u=null;return;}emit(name+'-enter',{unit:unit(this.u),flags:this.a.add(0x20).readU32(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.u)emit(name+'-leave',{unit:unit(this.u),flags:this.a.add(0x20).readU32()});}});
 }
 for(const [r,name] of [[0x6802f0,'ai-immediate'],[0x693450,'ai-append']])hook(r,{onEnter(){if(active&&mine(this.context.ecx))emit(name,{unit:unit(this.context.ecx),caller:this.returnAddress.sub(base).toString()});}});
 hook(0x687020,{onEnter(){this.u=active&&mine(this.context.ecx)?this.context.ecx:null;},onLeave(ret){if(this.u&&!ret.isNull())emit('ai-record',{unit:unit(this.u),flags:ret.add(0x28).readU32()});}});
 hook(0x603d70,{onEnter(){if(!active)return;this.traced=true;emit('find-transport-enter',{caller:this.returnAddress.sub(base).toString()});},onLeave(ret){if(this.traced)emit('find-transport-leave',{found:!ret.isNull()});}});
 hook(0x9c0b00,{onEnter(a){if(!active||!mine(a[0]))return;emit('guard-return',{unit:unit(a[0]),point:[this.context.ecx.add(0x28).readU32(),this.context.ecx.add(0x30).readU32()],caller:this.returnAddress.sub(base).toString()});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{counts,readOnly:true};}};
