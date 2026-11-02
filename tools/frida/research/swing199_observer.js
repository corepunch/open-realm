// Read-only explicit weapon-swing producer observer. Never calls game code.
let installed=false,active=false,recording=true,tick=0;
const counts={};
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Swing199 PE mismatch');
 installed=true;
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32()>>>0);
 const rva=p=>p.sub(base).toUInt32().toString(16);
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32()>>>0;
 const ability=p=>({id:words(p.add(0xc),2),unit:words(p.add(0x30).readPointer().add(0xc),2),flags:p.add(0x20).readU32(),slot:p.add(0x2b8).readU32(),request:p.add(0x3e4).readPointer().isNull()?null:words(p.add(0x3e4).readPointer().add(4),1)});
 const row=(event,data={})=>{if(active)emit(event,{tick,c:counter(),...data});};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;active=true;const m=value.match(/tick=(\d+)/);if(m)tick=+m[1];row('marker',{value});if(value.includes(' label=complete'))active=false;}});
 Interceptor.attach(base.add(0x49d130),{onEnter(args){this.live=active;if(!this.live)return;this.p=this.context.ecx;row('cooldown-begin',{ability:ability(this.p),duration:args[0].readU32(),swing:args[1].toUInt32(),caller:rva(this.returnAddress)});},onLeave(){if(this.live)row('cooldown-end',{ability:ability(this.p)});}});
 Interceptor.attach(base.add(0x49bc40),{onEnter(){this.live=active;if(!this.live)return;this.p=this.context.ecx;row('cap-begin',{ability:ability(this.p),caller:rva(this.returnAddress)});},onLeave(){if(this.live)row('cap-end',{ability:ability(this.p)});}});
 Interceptor.attach(base.add(0x05c350),{onEnter(args){row('cap-bit',{value:args[0].toUInt32(),caller:rva(this.returnAddress)});}});
 Interceptor.attach(base.add(0x0608d0),{onEnter(args){if(active&&args[1].toUInt32()===0xd01bd)row('cap-arm',{duration:args[0].readU32(),ability:ability(args[2])});}});
 emit('installed',{hooks:5});
}});
