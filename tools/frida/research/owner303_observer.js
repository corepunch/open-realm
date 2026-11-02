// Read-only lifecycle/ownership boundaries. No game calls or memory writes.
let base,installed=false,recording=true,window=false,scene=-1,actor=null,seq=0,depth=0,counts={};
const pair=p=>[p.readU32(),p.add(4).readU32()];
function payload(id){
 if(id.every(x=>x===0xffffffff))return ptr(0);
 const r=base.add(0xd68610).readPointer(),alt=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;
 if(i>=r.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const s=r.add(alt?0x2c:0xc).readPointer().add(i*8);if(s.readS32()!==-2)return ptr(0);
 const p=s.add(4).readPointer();return p.isNull()||pair(p.add(0x14)).some((x,i)=>x!==id[i])||p.add(0x20).readU32()?ptr(0):p.add(0x54).readPointer();
}
function abilities(u){
 const result=[];let id=pair(u.add(0x1dc));
 while(!id.every(x=>x===0xffffffff)){
  if(result.length>=256)throw Error('ability list unbounded');
  const p=payload(id);if(p.isNull())throw Error('unresolved ability');
  result.push({identity:id,vtable:p.readPointer().sub(base).toString(),classGetter:p.readPointer().add(0x1c).readPointer().sub(base).toString(),disabled:[0x3c,0x40,0x44].map(x=>p.add(x).readS32())});
  id=pair(p.add(0x24));
 }
 return result;
}
function state(u){
 const m=payload(pair(u.add(0x16c))),f=m.isNull()?ptr(0):m.add(0x98).readPointer();
 return {identity:pair(u.add(0xc)),code:u.add(0x30).readU32(),flags:u.add(0x5c).readU32(),suppression:u.add(0x198).readS32(),abilities:abilities(u),
  fine:f.isNull()?null:{identity:pair(f.add(0x14)),category:f.add(0x34).readU32(),counter:f.add(0x40).readU32(),box:[0x1c,0x20,0x24,0x28].map(x=>f.add(x).readS32())}};
}
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,scene,...data});}
Process.attachModuleObserver({onAdded(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 base=m.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const hook=(r,h)=>Interceptor.attach(base.add(r),h);
 hook(0x231df0,{onEnter(a){if(!recording||a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  scene=Number(/scene=(\d+)/.exec(value)[1]);if(value.includes('label=scene-start')){actor=null;window=false;}
  if(value.includes('label=before'))window=true;
  send({event:'marker',value});if(actor!==null&&window)emit('state',{state:state(actor)});
  if(value.includes('label=settled')||value.includes('label=complete'))window=false;
 }});
 for(const [r,name]of [[0x679bb0,'death'],[0x694690,'remove'],[0x69c510,'inactivity']])hook(r,{
  onEnter(){this.take=recording&&window;if(!this.take)return;actor=this.context.ecx;this.u=actor;emit(name+'-enter',{state:state(actor)});},
  onLeave(){if(this.take)emit(name+'-exit',{state:state(this.u)});}
 });
 hook(0x69a840,{onEnter(){this.take=recording&&window;if(this.take){depth++;emit('unit-stop-enter',{caller:this.returnAddress.sub(base).toString(),state:state(this.context.ecx)});}},onLeave(){if(this.take){emit('unit-stop-exit',{});depth--;}}});
 for(const [r,name]of [[0x5fc120,'move-cancel'],[0x600340,'move-cleanup'],[0x5fa7a0,'move-arrival'],[0x5fda10,'move-dispatch']])hook(r,{onEnter(a){if(recording&&window)emit(name,{caller:this.returnAddress.sub(base).toString(),task_event:name==='move-dispatch'?a[0].add(8).readU32():null});}});
 hook(0x16eb20,{onEnter(){if(recording&&window){emit('destroy',{});actor=null;}}});
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,depth,counts};}};
