// ORDER-01.13: read-only public spell heads and incoming ability ownership.
// No game calls, output writes, register changes or forced state.
let installed=false,active=false,recording=true,scene=-1;
const counts={};
function emit(event,data={}) {if(recording) {counts[event]=(counts[event]||0)+1;send({event,scene,...data});}}
Process.attachModuleObserver({onAdded(module) {
 if(installed || module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4 || pe.add(8).readU32()!==config.timestamp || pe.add(80).readU32()!==config.imageSize)throw Error('Owner268 PE differs');
 installed=true;emit('module',{base:base.toString(),path:module.path});
 Interceptor.attach(base.add(0x231df0),{onEnter(args) {
  if(args[0].isNull())return;
  const value=args[0].readCString();if(!value || !value.startsWith(config.prefix))return;
  const m=value.match(/ s=(-?\d+)/);if(m)scene=Number(m[1]);
  if(value.includes(' label=begin-setup'))active=true;
  emit('marker',{value});
  if(value.includes(' label=end-cleanup'))active=false;
 }});
 Interceptor.attach(base.add(0x679cc0),{onEnter(args) {
  this.row=active ? {unit:this.context.ecx.toString(),order:args[0].toString(),command:args[0].add(0x24).readU32(),vtable:args[0].readPointer().sub(base).toUInt32()} : null;
  if(this.row)emit('ability-order',this.row);
 },onLeave(ret) {if(this.row)emit('ability-order-result',{...this.row,result:ret.toUInt32()});}});
 Interceptor.attach(base.add(0x2039d0),{onEnter(args) {this.handle=active ? args[0].toUInt32() : null;},onLeave(ret) {if(this.handle!==null)emit('current-query',{handle:this.handle,command:ret.toUInt32()});}});
 Interceptor.attach(base.add(0x438680),{onEnter(args) {if(active)emit('target-producer',{ability:this.context.ecx.toString(),packet:args[0].toString()});}});
}});
rpc.exports={finish() {recording=false;return {installed,active,counts};}};
