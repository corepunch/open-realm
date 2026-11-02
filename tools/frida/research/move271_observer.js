// Read-only temporary movement-profile observer; no game calls or memory writes.
let installed=false,active=false,tick=0,seq=0,depth=0;const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 emit('module');
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);active=true;emit('marker',{value});if(value.includes('label=complete'))active=false;}});
 hook(0x5fc900,{onEnter(args){if(!active)return;const owner=this.context.ecx.add(0x30).readPointer();if(owner.isNull()||owner.add(0x30).readU32()!==0x68503237)return;this.track=true;this.out=args[0];depth++;emit('speed-input',{base:this.context.ecx.add(0x70).readU32(),factor:this.context.ecx.add(0x78).readU32(),caller:this.returnAddress.sub(base).toUInt32().toString(16)});},onLeave(){if(this.track){emit('speed-output',{word:this.out.readU32()});depth--;}}});
 for(const [r,event]of [[0x68f010,'minimum'],[0x68ed40,'maximum']])hook(r,{onEnter(){if(!depth)return;this.track=true;this.out=this.context.ecx;this.rawcode=this.context.edx.toUInt32();},onLeave(){if(this.track)emit(event,{rawcode:this.rawcode,word:this.out.readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,readOnly:true,counts,depth};}};
