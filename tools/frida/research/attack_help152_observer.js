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
 const unit=p=>p.isNull()?null:{address:p.toString(),owner:p.add(0x58).readU32(),flags:p.add(0x20).readU32(),
    status:p.add(0x5c).readU32(),ownerGetter:rva(p.readPointer().add(0xec).readPointer())};
 hook(0x66e700,{onEnter(args){if(active)emit('help',{victim:unit(this.context.ecx),source:unit(args[0]),mode:args[1].toUInt32()});}});
 hook(0x66e9e0,{onEnter(){if(active){const p=this.context.edx;emit('candidate',{helper:unit(this.context.ecx),source:unit(p.readPointer()),victim:unit(p.add(4).readPointer())});}}});
 hook(0x49bb50,{onEnter(args){if(!active)return;this.ability=this.context.ecx;this.before=state(this.ability);
    this.helper=this.ability.add(0x30).readPointer();this.source=args[0];this.victim=args[1];},
    onLeave(){if(this.ability)emit('ally',{helper:unit(this.helper),source:unit(this.source),victim:unit(this.victim),before:this.before,after:state(this.ability)});}});
 for(const [address,kind] of [[0x695200,'request'],[0x695600,'response'],[0x68bd60,'enemy']])hook(address,{
    onEnter(args){this.track=active;if(this.track){this.actor=this.context.ecx;this.argument=args[0];}},
    onLeave(result){if(this.track)emit('gate',{kind,actor:unit(this.actor),argument:this.argument.toString(),result:result.toUInt32()});}});

 hook(0x688060,{onEnter(){if(active){this.output=this.context.ecx;this.victim=this.context.edx;}},
   onLeave(){if(this.output)emit('help-radius',{victim:unit(this.victim),radius:this.output.readU32(),normalDelay:base.add(0xd3c750).readU32(),aiRadius:base.add(0xd3c7f4).readU32()});}});
 hook(0x0608d0,{onEnter(args){this.track=active&&args[1].toUInt32()===0xd01b3;
   if(this.track){this.victim=args[2];this.delay=args[0].readU32();}},
   onLeave(){if(this.track)emit('help-arm',{victim:unit(this.victim),delay:this.delay});}});
 hook(0x15ba40,{onLeave(value){if(active)emit('unit-map',{cell:value.add(0x68).readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,counts};}};
