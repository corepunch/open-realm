// Read-only Attack exemption timer observer. No native calls or writes.
let active=false,installed=false,tick=0,seq=0;const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});}
function install(mod){
 if(installed||mod.name.toLowerCase()!=='game.dll')return;
 const base=mod.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE mismatch');
 installed=true;const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 const rva=p=>p.sub(base).toUInt32().toString(16);
 const state=ability=>{const request=ability.add(0x3e4).readPointer();
  if(request.isNull())return {active:false};
  const clock=request.add(0xc).readPointer();
  return {active:!(request.add(0x10).readU32()&0x10000),deadline:request.add(4).readU32(),
    clock:clock.add(0x40).readU32(),serial:request.add(0x14).readU32(),request:request.toString()};};
 emit('module',{base:base.toString()});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();
  if(!value||!value.startsWith(config.prefix))return;
  const t=/tick=(\d+)/.exec(value);if(t)tick=Number(t[1]);
  if(value.includes('label=start'))active=true;emit('marker',{value});}});
 hook(0x49bc40,{onEnter(){if(!active)return;this.ability=this.context.ecx;this.before=state(this.ability);
  this.caller=rva(this.returnAddress);},onLeave(){if(this.ability)emit('begin',{ability:this.ability.toString(),
    caller:this.caller,before:this.before,after:state(this.ability)});}});
 hook(0x497310,{onEnter(){if(active)emit('expire',{ability:this.context.ecx.toString(),before:state(this.context.ecx)});}});
 hook(0x05c350,{onEnter(args){this.track=active;if(this.track){this.value=args[0].toUInt32();this.caller=rva(this.returnAddress);}},
  onLeave(){if(this.track)emit('exempt',{value:this.value,caller:this.caller,flags:this.context.edx.add(0xd8).readU32()});}});
 hook(0x4935e0,{onEnter(args){if(active)emit('notice',{flags:args[0].add(0xc).readU32(),damage:args[0].add(0x10).readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,counts};}};
