// Read-only public Mechanical Critter item producer and latent separation policy.
let installed=false,active=false,recording=true,tick=0,choosing=false,pending=null;
const counts={};
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Mechanical201 PE mismatch');
 installed=true;
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32()>>>0);
 const rva=p=>p.sub(base).toUInt32().toString(16);
 const movers=new Map();let refreshed=null;
 const sep=m=>{const p=m.add(0xac).readPointer();return p.isNull()?null:p.add(0x20).readU32();};
 const unit=p=>({id:words(p.add(0xc),2),code:p.add(0x30).readU32(),owner:p.add(0x58).readU32(),flag:p.add(0x60).readU32(),depth:p.add(0xec).readS32(),sep:movers.has(p.toString())?sep(movers.get(p.toString())):null});
 const row=(event,data={})=>{if(active)emit(event,{tick,...data});};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;active=true;const m=value.match(/tick=(\d+)/);if(m)tick=+m[1];row('marker',{value});if(value.includes(' label=complete'))active=false;}});
 const flush=()=>{if(pending){row('candidate',{code:pending.readU32()});pending=null;}};
 Interceptor.attach(base.add(0x672dd0),{onEnter(){this.live=active;if(!this.live)return;choosing=true;row('choose-begin',{level:this.context.ecx.toInt32(),tileset:this.context.edx.toUInt32()&255,args:words(this.context.esp.add(4),3)});},onLeave(ret){if(this.live){flush();row('choose-end',{code:ret.toUInt32()});choosing=false;}}});
 // Observe the append return, then read the caller's store before the next append can grow its array.
 Interceptor.attach(base.add(0x139120),{onEnter(){this.live=choosing;if(this.live)flush();},onLeave(ret){if(this.live)pending=ptr(ret.toString());}});
 Interceptor.attach(base.add(0x693660),{onEnter(){this.live=active;if(choosing)flush();if(this.live)row('range-begin',{index:this.context.ecx.toUInt32(),span:this.context.edx.toUInt32(),caller:rva(this.returnAddress)});},onLeave(ret){if(this.live)row('range-end',{value:ret.toUInt32()});}});
 Interceptor.attach(base.add(0x57f410),{onEnter(args){this.live=active;if(!this.live)return;this.u=args[0];row('apply-begin',{unit:unit(this.u),duration:args[1].readU32()});},onLeave(){if(this.live)row('apply-end',{unit:unit(this.u)});}});
 Interceptor.attach(base.add(0x58dfa0),{onEnter(){this.live=active;if(!this.live)return;this.u=this.context.ecx.add(0x30).readPointer();row('inverse-begin',{unit:unit(this.u)});},onLeave(){if(this.live)row('inverse-end',{unit:unit(this.u)});}});
 Interceptor.attach(base.add(0x693d50),{onEnter(){this.live=active;if(!this.live)return;this.u=this.context.ecx;refreshed=this.u;row('refresh',{unit:unit(this.u),caller:rva(this.returnAddress)});},onLeave(){if(this.live)refreshed=null;}});
 Interceptor.attach(base.add(0x1710e0),{onEnter(args){this.live=active;if(!this.live)return;this.m=this.context.ecx;this.u=refreshed;this.args=words(this.context.esp.add(4),4);},onLeave(){if(this.live){if(this.u)movers.set(this.u.toString(),this.m);row('configure',{args:this.args,sep:sep(this.m)});}}});
 if(config.pose_words) for(const [offset,event] of [[0x204100,'public-x'],[0x204140,'public-y']]) {
  Interceptor.attach(base.add(offset),{onEnter(args){this.live=active;this.handle=args[0].toUInt32();},onLeave(ret){if(this.live)row(event,{handle:this.handle,word:ret.toUInt32()});}});
 }
 emit('installed',{hooks:config.pose_words?11:9});
}});
