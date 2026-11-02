// Read-only concrete-class witnesses for public spell-owned buff construction.
let installed=false,tick=0,scene=-1,seq=0,depth=0;const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const rva=p=>p.sub(base).toUInt32().toString(16),hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){if(a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(-?\d+)/.exec(value)[1]);emit('marker',{value});}});
 hook(0x925500,{onEnter(a){emit('compile-error',{code:a[1].toUInt32(),line:a[0].add(0xb8).readU32()});}});
 for(const [address,name]of [[0x52e900,'stun-factory'],[0x510ed0,'sleep-stun-factory']])hook(address,{onEnter(){this.track=scene>=0&&scene<8;if(this.track){depth++;emit(name,{caller:rva(this.returnAddress)});}},onLeave(v){if(this.track){emit(name+'-end',{present:!v.isNull(),vtable:v.isNull()?null:rva(v.readPointer())});depth--;}}});
 for(const [address,name]of [[0x530570,'stun-initialize'],[0x5314b0,'pause-initialize'],[0x518290,'sleep-initialize'],[0x530260,'sleep-pause-initialize'],[0x518320,'sleep-pause-guard']])hook(address,{onEnter(a){if(scene<0||scene>=8)return;this.track=true;depth++;const b=this.context.ecx,u=a[0];emit(name,{vtable:rva(b.readPointer()),caller:rva(this.returnAddress),target:u.isNull()?null:{slot:u.add(0xc).readU32(),generation:u.add(0x10).readU32(),rawcode:u.add(0x30).readU32(),owner:u.add(0x58).readU32()}});},onLeave(){if(this.track)depth--;}});
 emit('module',{base:base.toString()});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){return {installed,readOnly:true,counts,depth};}};
