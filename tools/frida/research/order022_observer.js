// ORDER-02.2 read-only original UI packet, admission, owner-change and interception observer.
// No NativeFunction, game calls, return replacement or memory writes.
let installed=false,recording=true,started=false,tick=0,base,seq=0,depth=0;
const pair=p=>[p.readU32(),p.add(4).readU32()];
const emit=(event,data={})=>send({event,seq:++seq,tick,...data});
const TYPES=new Set([0x68666f6f,0x68706561]); // hfoo, hpea
function wrapper(id){
 if(id.every(x=>x===0xffffffff))return ptr(0);
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(0x14)).some((x,i)=>x!==id[i])?ptr(0):p;
}
function payload(id){const w=wrapper(id);return w.isNull()||w.add(0x20).readU32()?ptr(0):w.add(0x54).readPointer();}
function order(p){
 if(p.isNull())return null;
 const vtable=p.readPointer().sub(base).toUInt32();
 return {identity:pair(p.add(0xc)),vtable,flags:p.add(0x20).readU32(),command:p.add(0x24).readU32(),
  next:pair(p.add(0x2c)),...(vtable===0xb787dc?{}:{point:[p.add(0x48).readU32(),p.add(0x50).readU32()],target:pair(p.add(0x58))})};
}
function state(u){
 const head=pair(u.add(0x19c)),tail=pair(u.add(0x1a8)),task=pair(u.add(0x174)),t=payload(task);
 return {user:head,tail,count:u.add(0x1b4).readU32(),tasks:task,head_order:order(payload(head)),
  tail_order:order(payload(tail)),task_code:t.isNull()?null:t.add(0x30).readU32(),flags:u.add(0x5c).readU32(),
  widget:u.add(0x20).readU32(),depth:u.add(0x198).readS32(),owner:u.add(0x58).readU32()};
}
const mine=u=>recording&&started&&!u.isNull()&&TYPES.has(u.add(0x30).readU32());
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;emit('module',{base:base.toString(),path:m.path});
 const rel=p=>p.sub(base).toUInt32();
 const hook=(r,callbacks)=>Interceptor.attach(base.add(r),callbacks);
 const unitHook=(rva,name,args)=>hook(rva,{onEnter(a){this.u=this.context.ecx;this.take=mine(this.u);if(!this.take)return;
  emit(name,{unit:this.u.toString(),caller:rel(this.returnAddress),...(args?args.call(this,a):{}),state:state(this.u),depth:++depth});
 },onLeave(r){if(this.take)emit(name+'-end',{unit:this.u.toString(),result:r.toUInt32(),state:state(this.u),depth:depth--});}});
 hook(0x231df0,{onEnter(a){if(a[0].isNull())return;const value=a[0].readCString();if(recording&&value.startsWith(config.prefix)){
  started=true;const hit=/tick=(\d+)/.exec(value);if(hit)tick=Number(hit[1]);emit('marker',{value});
 }}});
 // 6b93a0 fastcall: ECX unit, EDX order, stack packet flags and fallback.
 unitHook(0x6b93a0,'net-order',function(a){return{order:order(this.context.edx),flags:a[0].toUInt32(),fallback:a[1].toUInt32()};});
 // 6b8cc0 fastcall: ECX unit, EDX command, stack f20, f2, f40, selection, ctx3c.
 unitHook(0x6b8cc0,'fallback-test',function(a){return{command:this.context.edx.toUInt32(),f20:a[0].toUInt32(),f2:a[1].toUInt32(),
  f40:a[2].toUInt32(),ctx3c:a[4].toUInt32()};});
 unitHook(0x680320,'admit',a=>({order:order(a[0]),mode:a[1].toUInt32(),dispatch:a[2].toUInt32()}));
 unitHook(0x69b2f0,'intercept',a=>({order:order(a[0])}));
 unitHook(0x693490,'append',a=>({order:order(a[0])}));
 unitHook(0x691c70,'insert',a=>({order:order(a[0]),after_head:a[1].toUInt32(),dispatch:a[2].toUInt32()}));
 unitHook(0x67abe0,'dispatch',a=>({order:order(a[0]),dispatch:a[1].toUInt32()}));
 unitHook(0x673e80,'cancel-successors');
 unitHook(0x673610,'cancel-all');
 unitHook(0x698ce0,'set-owner',a=>({new_owner:a[0].toUInt32()}));
 unitHook(0x48ef40,'begin-channel-work');
 for(const [rva,name]of[[0x67bd10,'immediate-event'],[0x67c230,'point-event'],[0x67d2b0,'target-event']])
  hook(rva,{onEnter(a){if(mine(this.context.ecx))emit(name,{unit:this.context.ecx.toString(),order:order(a[0]),state:state(this.context.ecx)});}});
}});
rpc.exports={finish(){recording=false;return{installed,depth};}};
