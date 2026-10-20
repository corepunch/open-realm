// Retail1.27 actual command range/heading/forced-arrival audit. Read only.
let installed=false, recording=true, tick=-1, scene=-1;
const counts={};
function emit(event,data={}) {if(recording) {counts[event]=(counts[event]||0)+1;send({event,tick,scene,...data});}}
const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32());
function install(module) {
 if(installed || module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4 || pe.add(8).readU32()!==config.timestamp || pe.add(80).readU32()!==config.imageSize)throw Error('wrong original image');
 installed=true;emit('module',{base:base.toString(),path:module.path});
 const at=rva=>base.add(rva),rel=p=>p.sub(base).toUInt32();
 const clock=()=>{const owner=at(0xd53a48).readPointer();return owner.isNull()?null:{counter:owner.add(0x538).readU32(),primary:words(owner.add(0x54),4)};};
 const state=m=>({mover:m.toString(),pose:words(m.add(0x70),8),heading:m.add(0x8c).readU32(),radius:m.add(0x90).readU32(),range:m.add(0xb0).readU32(),flags:m.add(0xd8).readU32(),group:words(m.add(0x9c),2),request:words(m.add(0xc0),2)});
 const hook=(rva,methods)=>Interceptor.attach(at(rva),methods);
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith('T371 '))return;tick=parseInt((value.match(/tick=(\d+)/)||[])[1]||'-1');scene=parseInt((value.match(/scene=(\d+)/)||[])[1]||'-1');emit('marker',{value,clock:clock()});}});
 hook(0x692120,{onEnter(args){emit('point-task',{unit:this.context.ecx.toString(),code:args[0].toUInt32(),point:[args[1].readU32(),args[2].readU32()],range:args[3].readU32(),association:args[4].toString(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x49a980,{onEnter(args){emit('attack-command',{ability:this.context.ecx.toString(),unit:this.context.ecx.add(0x30).readPointer().toString(),code:args[0].add(8).readU32(),clock:clock()});}});
 hook(0x05b970,{onEnter(args){emit('point-request',{bridge:this.context.ecx.toString(),identity:words(this.context.ecx.add(8),2),point:[args[0].readU32(),args[1].readU32()],arrival:args[2].toUInt32(),cantpath:args[3].toUInt32(),receiver:args[4].toString(),speed:args[5].readU32(),flag:args[6].toUInt32(),range:args[7].readU32(),caller:rel(this.returnAddress),clock:clock()});}});
 hook(0x1710a0,{onEnter(args){this.m=this.context.ecx;this.row={value:args[0].readU32(),before:state(this.m),caller:rel(this.returnAddress),clock:clock()};},onLeave(){emit('range-publication',{...this.row,after:state(this.m)});}});
 hook(0x16e910,{onEnter(args){this.m=this.context.ecx;this.angle=args[4];this.inside=args[5];this.row={source:words(args[0],2),heading:args[1].readU32(),threshold:args[2].readU32(),destination:words(args[3],2),before:state(this.m),clock:clock()};},onLeave(ret){emit('arrival',{...this.row,result:ret.toUInt32(),angle:this.angle.readU32(),inside:this.inside.readU32()});}});
 hook(0x16fe20,{onEnter(args){this.m=this.context.ecx;this.row={speed:args[0].readU32(),heading:args[1].readU32(),before:state(this.m),clock:clock()};},onLeave(){emit('velocity',{...this.row,after:state(this.m)});}});
 for(const [rva,name]of[[0x171060,'force-range'],[0x16f560,'clear-force'],[0x171340,'stop']])hook(rva,{onEnter(){this.m=this.context.ecx;this.row={before:state(this.m),caller:rel(this.returnAddress),clock:clock()};},onLeave(){emit(name,{...this.row,after:state(this.m)});}});
 for(const [rva,name]of[[0x5fa7a0,'move-arrival'],[0x603110,'move-cantpath']])hook(rva,{onEnter(args){emit(name,{ability:this.context.ecx.toString(),unit:this.context.ecx.add(0x30).readPointer().toString(),clock:clock(),caller:rel(this.returnAddress)});}});
 hook(0x170880,{onEnter(args){this.m=this.context.ecx;this.a=[args[0],args[1],args[2]];this.row={before:state(this.m),input:this.a.map(p=>p.readU32()),stop:args[3].toUInt32(),caller:rel(this.returnAddress),clock:clock()};},onLeave(){emit('turn-or-stop',{...this.row,output:this.a.map(p=>p.readU32()),after:state(this.m)});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={status(){return{installed,counts};},finish(){recording=false;return{installed,counts};}};
