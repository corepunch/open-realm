// FORM-01.3 read-only policy -> group coarse warp-mode observer. No game calls/writes.
let active=false, installed=false, lane=0, tick=0;
function install(module) {
 if(installed || module.name.toLowerCase()!=="game.dll")return;
 const base=module.base, pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4 || pe.add(8).readU32()!==config.timestamp || pe.add(80).readU32()!==config.imageSize)throw Error("PE mismatch");
 installed=true;send({event:"module",base:base.toString()});
 const emit=(event,data)=>send({event,lane,tick,...data});
 const hook=(r,callbacks)=>Interceptor.attach(base.add(r),callbacks);
 hook(0x231df0,{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;
  const m=value.match(/tick=(\d+).*lane=(\d+)/);if(m){tick=Number(m[1]);lane=Number(m[2]);}
  if(value.includes(" label=start"))active=true;
  emit("marker",{value});if(value.includes(" label=complete"))active=false;
 }});
 hook(0x16ce10,{onEnter(){if(active)this.group=this.context.ecx;},onLeave(result){if(this.group)emit("group-route",{flags:this.group.add(0x80).readU32(),result:result.toUInt32()});}});
 hook(0x167120,{onEnter(args){if(!active)return;this.path=this.context.ecx;this.warp=args[1].toUInt32();},onLeave(result){
  if(this.path)emit("coarse-request",{warp:this.warp,result:result.toUInt32(),flags:this.path.add(0x88).readU32(),count:this.path.add(0x70).readU32()});
 }});
}
Process.attachModuleObserver({onAdded:install});

rpc.exports={finish(){return {installed, tick, lane, active};}};
