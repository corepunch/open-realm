// Read-only Patrol task composition; no native calls or target writes.
let installed=false,recording=true,depth=0,patrols=0,points=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const b=m.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:b.toString(),path:m.path});
 const hook=(r,c)=>Interceptor.attach(b.add(r),c);
 hook(0x231df0,{onEnter(args){if(!recording||args[0].isNull())return;const value=args[0].readCString();if(value.startsWith(config.prefix))send({event:'marker',value});}});
 hook(0x170fa0,{onEnter(args){this.group=args[0];this.mover=this.context.ecx;},onLeave(){if(recording&&!this.group.isNull())send({event:'physical-bind',mover:this.mover.toString(),identity:[this.group.add(0x14).readU32(),this.group.add(0x18).readU32()],count:this.group.add(0x38).readU32()});}});
 hook(0x5fdff0,{onEnter(args){depth++;if(!recording)return;patrols++;send({event:'patrol-leg',unit:this.context.ecx.add(0x30).readPointer().toString()});},onLeave(){depth--;}});
 hook(0x692120,{onEnter(args){if(!recording||!depth)return;points++;send({event:'patrol-point',unit:this.context.ecx.toString(),code:args[0].toUInt32(),x:args[1].readU32(),y:args[2].readU32(),range:args[3].readU32(),association:args[4].toString()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,patrols,points};}};
