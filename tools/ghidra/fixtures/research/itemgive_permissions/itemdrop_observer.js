// Retail1.27 public UnitDropItemPoint producer, physical point task and drop sink. Read only.
let installed=false,recording=true,tick=-1,scene=-1;const counts={};
function emit(event,data={}){if(recording&&tick>=0){counts[event]=(counts[event]||0)+1;send({event,tick,scene,...data});}}
const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32());
function install(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('wrong original image');
 installed=true;
 const at=r=>base.add(r),rel=p=>p.sub(base).toUInt32();
 const clock=()=>{const o=at(0xd53a48).readPointer();return o.isNull()?null:{counter:o.add(0x538).readU32(),primary:words(o.add(0x54),4)};};
 const unit=u=>u.isNull()?null:{pointer:u.toString(),identity:words(u.add(12),2),rawcode:u.add(0x30).readU32(),flags:u.add(0x5c).readU32(),head:words(u.add(0x19c),2),task:words(u.add(0x174),2)};
 const state=m=>({mover:m.toString(),pose:words(m.add(0x70),8),heading:m.add(0x8c).readU32(),radius:m.add(0x90).readU32(),range:m.add(0xb0).readU32(),flags:m.add(0xd8).readU32(),group:words(m.add(0x9c),2),request:words(m.add(0xc0),2)});
 const hook=(rva,methods)=>Interceptor.attach(at(rva),methods);
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;tick=parseInt((value.match(/tick=(\d+)/)||[])[1]||'-1');scene=parseInt((value.match(/scene=(-?\d+)/)||[])[1]||'-1');emit('marker',{value,clock:clock()});}});
 hook(0x218980,{onEnter(args){emit('public-drop-target',{unitHandle:args[0].toUInt32(),itemHandle:args[1].toUInt32(),targetHandle:args[2].toUInt32(),caller:rel(this.returnAddress),clock:clock()});},onLeave(ret){emit('public-drop-target-return',{result:ret.toUInt32(),clock:clock()});}});
 hook(0x05b340,{onEnter(args){this.row={source:this.context.ecx.toString(),target:args[0].toString(),window:args[1].isNull()?null:args[1].readU32(),caller:rel(this.returnAddress),clock:clock()};},onLeave(ret){emit('facing-query',{...this.row,result:ret.toUInt32()});}});
 hook(0x54efe0,{onEnter(){this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();emit('drop-producer-enter',{ability:this.a.toString(),vtable:rel(this.a.readPointer()),actor:unit(this.u),caller:rel(this.returnAddress),clock:clock()});},onLeave(){emit('drop-producer-leave',{actor:unit(this.u),clock:clock()});}});
 hook(0x56a670,{onEnter(){this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();emit('drop-action-enter',{ability:this.a.toString(),actor:unit(this.u),caller:rel(this.returnAddress),clock:clock()});},onLeave(){emit('drop-action-leave',{actor:unit(this.u),clock:clock()});}});
 for(const [rva,name] of [[0x56aa50,'give-action'],[0x56ab80,'pawn-action']])hook(rva,{onEnter(args){this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();this.task=args[0].add(0xc).readPointer();emit(name+'-enter',{actor:unit(this.u),task:this.task.toString(),taskWords:words(this.task,18),range:this.task.add(0x44).readU32(),caller:rel(this.returnAddress),clock:clock()});},onLeave(){emit(name+'-leave',{actor:unit(this.u),clock:clock()});}});
 hook(0x05b580,{onEnter(args){this.row={source:this.context.ecx.toString(),range:args[0].readU32(),target:args[1].toString(),predict:args[2].toUInt32(),caller:rel(this.returnAddress),clock:clock()};},onLeave(ret){emit('target-range-query',{...this.row,result:ret.toUInt32()});}});
 hook(0x545ed0,{onEnter(args){this.row={ability:this.context.ecx.toString(),target:args[0].toString(),item:args[1].toString(),caller:rel(this.returnAddress),clock:clock()};},onLeave(ret){emit('target-validation',{...this.row,result:ret.toUInt32()});}});
 hook(0x202520,{onLeave(ret){emit('get-sold-item',{value:ret.toUInt32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x56b690,{onEnter(args){emit('drop-sink',{ability:this.context.ecx.toString(),item:args[0].toString(),x:args[1].readU32(),y:args[2].readU32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x692120,{onEnter(args){emit('point-task',{unit:unit(this.context.ecx),code:args[0].toUInt32(),point:[args[1].readU32(),args[2].readU32()],range:args[3].readU32(),association:args[4].toString(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x692270,{onEnter(args){emit('action-task',{unit:unit(this.context.ecx),code:args[0].toUInt32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x692570,{onEnter(args){emit('target-item-action-task',{unit:unit(this.context.ecx),code:args[0].toUInt32(),target:args[1].toString(),item:args[2].toString(),range:args[3].readU32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x66ffd0,{onEnter(){this.u=this.context.ecx;},onLeave(ret){emit('can-get-items',{unit:unit(this.u),value:ret.toUInt32(),clock:clock()});}});
 hook(0x6926b0,{onEnter(args){emit('target-task',{unit:unit(this.context.ecx),code:args[0].toUInt32(),target:args[1].toString(),range:args[2].readU32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x05b970,{onEnter(args){emit('point-request',{bridge:this.context.ecx.toString(),point:[args[0].readU32(),args[1].readU32()],arrival:args[2].toUInt32(),cantpath:args[3].toUInt32(),receiver:args[4].toString(),speed:args[5].readU32(),flag:args[6].toUInt32(),range:args[7].readU32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x1710a0,{onEnter(args){this.m=this.context.ecx;this.row={value:args[0].readU32(),before:state(this.m),caller:rel(this.returnAddress),clock:clock()};},onLeave(){emit('range-publication',{...this.row,after:state(this.m)});}});
 hook(0x16e910,{onEnter(args){this.m=this.context.ecx;this.angle=args[4];this.inside=args[5];this.row={source:words(args[0],2),heading:args[1].readU32(),threshold:args[2].readU32(),destination:words(args[3],2),before:state(this.m),clock:clock()};},onLeave(ret){emit('arrival',{...this.row,result:ret.toUInt32(),angle:this.angle.readU32(),inside:this.inside.readU32()});}});
 hook(0x16fe20,{onEnter(args){this.m=this.context.ecx;this.row={speed:args[0].readU32(),heading:args[1].readU32(),before:state(this.m),clock:clock()};},onLeave(){emit('velocity',{...this.row,after:state(this.m)});}});
 for(const [rva,name]of[[0x171060,'force-range'],[0x16f560,'clear-force'],[0x171340,'stop']])hook(rva,{onEnter(){this.m=this.context.ecx;this.row={before:state(this.m),caller:rel(this.returnAddress),clock:clock()};},onLeave(){emit(name,{...this.row,after:state(this.m)});}});
 for(const [rva,name]of[[0x5fa7a0,'move-arrival'],[0x603110,'move-cantpath']])hook(rva,{onEnter(){emit(name,{ability:this.context.ecx.toString(),unit:unit(this.context.ecx.add(0x30).readPointer()),clock:clock(),caller:rel(this.returnAddress)});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return{installed,counts};}};
