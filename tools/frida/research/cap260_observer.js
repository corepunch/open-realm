// Read-only original availability/acquisition observer. No native calls/writes.
let installed=false,active=false,tick=0,scene=-1,seq=0,depth=0;const counts={},ids=new Map();
function id(p){if(p.isNull())return -1;const k=p.toString();if(!ids.has(k))ids.set(k,ids.size);return ids.get(k);}
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const rva=p=>p.sub(base).toUInt32().toString(16),hook=(r,h)=>Interceptor.attach(base.add(r),h);
 const unit=u=>({identity:id(u),widget:u.add(0x20).readU32(),flags:u.add(0x5c).readU32(),suspension:u.add(0x54).readU32(),x:u.add(0x284).readU32(),y:u.add(0x288).readU32()});
 const state=a=>{const r=a.add(0x3e4).readPointer();return r.isNull()?{active:false}:{active:!(r.add(0x10).readU32()&0x10000),deadline:r.add(4).readU32(),serial:r.add(0x14).readU32()};};
 emit('module',{base:base.toString()});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;
  tick=Number(/tick=(\d+)/.exec(value)[1]);scene=Number(/scene=(\d+)/.exec(value)[1]);
  if(value.includes('label=owner-before'))active=true;emit('marker',{value});if(value.includes('label=owner-after'))active=false;
 }});
 hook(0x6510b0,{onEnter(){if(!active)return;this.track=true;emit('available-enter',{source:unit(this.context.ecx),caller:rva(this.returnAddress),radius:base.add(0xd70744).readU32()});},onLeave(){if(this.track)emit('available-exit');}});
 hook(0x49e130,{onEnter(args){if(!active)return;this.track=true;this.a=this.context.ecx;this.u=this.a.add(0x30).readPointer();depth++;
  emit('notify-enter',{receiver:unit(this.u),target:unit(args[0].add(0xc).readPointer()),ability_flags:this.a.add(0x20).readU32(),prevention:this.a.add(0x220).readU32(),acquisition:this.a.add(0x244).readU32(),target_words:[this.a.add(0x6c).readU32(),this.a.add(0x70).readU32()],before:state(this.a)});
 },onLeave(){if(this.track){emit('notify-exit',{receiver:id(this.u),after:state(this.a)});depth--;}}});
 hook(0x49bc40,{onEnter(){if(active&&depth)emit('begin',{receiver:id(this.context.ecx.add(0x30).readPointer()),caller:rva(this.returnAddress)});}});
 hook(0x4959f0,{onEnter(args){if(!active||!depth)return;this.track=true;this.u=this.context.ecx.add(0x30).readPointer();this.target=args[0];},onLeave(v){if(this.track)emit('admission',{receiver:id(this.u),target:id(this.target),accepted:v.toUInt32()});}});
 hook(0x05b580,{onEnter(args){if(!active||!depth)return;this.track=true;this.range=args[0].readU32();this.mode=args[2].toUInt32();this.caller=rva(this.returnAddress);},onLeave(v){if(this.track)emit('range',{range:this.range,mode:this.mode,caller:this.caller,accepted:v.toUInt32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){active=false;return {installed,readOnly:true,counts,depth};}};
