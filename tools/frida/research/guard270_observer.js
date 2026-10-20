// Read-only acquisition guard observer. Never calls game code or writes memory.
let installed=false,active=false,tick=0,seq=0,depth=0;const counts={};
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const hook=(r,h)=>Interceptor.attach(base.add(r),h),rva=p=>p.sub(base).toUInt32().toString(16);
 const unit=u=>({x:u.add(0x284).readU32(),y:u.add(0x288).readU32(),move:!u.add(0x1ec).readPointer().isNull()});
 emit('module',{minimum:base.add(0xd6bf64).readU32()});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);if(value.includes('label=owner-before'))active=true;emit('marker',{value});if(value.includes('label=owner-after'))active=false;}});
 hook(0x49e130,{onEnter(args){if(!active)return;this.track=true;depth++;emit('notify',{actor:unit(this.context.ecx.add(0x30).readPointer()),candidate:unit(args[0].add(0xc).readPointer())});},onLeave(){if(this.track)depth--;}});
 hook(0x66f030,{onEnter(args){if(!active||!depth)return;this.track=true;this.actor=unit(this.context.ecx);this.args=[args[0].toUInt32(),args[1].toUInt32()];},onLeave(v){if(this.track)emit('mobility',{actor:this.actor,args:this.args,result:v.toUInt32()});}});
 hook(0x498520,{onEnter(args){if(!active||!depth)return;this.track=true;this.out=args[0];},onLeave(){if(this.track)emit('weapon-range',{word:this.out.readU32()});}});
 hook(0x05b580,{onEnter(args){if(!active||!depth)return;this.track=true;this.range=args[0].readU32();this.mode=args[2].toUInt32();this.caller=rva(this.returnAddress);},onLeave(v){if(this.track)emit('range',{range:this.range,mode:this.mode,caller:this.caller,result:v.toUInt32()});}});
 hook(0x49bc40,{onEnter(){if(active&&depth)emit('exempt',{actor:unit(this.context.ecx.add(0x30).readPointer())});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,readOnly:true,counts,depth};}};
