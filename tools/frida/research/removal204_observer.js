// Read-only removal/suppression ordering; no game calls or memory writes.
let installed=false,active=false,recording=true,tick=0;
const counts={};
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Removal204 PE mismatch');
 installed=true;
 const movers=new Map(),watched=new Map();let refreshed=null;
 const rva=p=>p.sub(base).toUInt32();
 const sep=m=>{const p=m.add(0xac).readPointer();return p.isNull()?null:p.add(0x20).readU32();};
 const unit=u=>({pointer:u.toString(),identity:[u.add(0xc).readU32(),u.add(0x10).readU32()],
  code:u.add(0x30).readU32(),owner:u.add(0x58).readU32(),f20:u.add(0x20).readU32(),
  f54:u.add(0x54).readS32(),f5c:u.add(0x5c).readU32(),depth:u.add(0x198).readS32(),
  sep:movers.has(u.toString())?sep(movers.get(u.toString())):null});
 const row=(event,data={})=>{if(active)emit(event,{tick,...data});};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();
  if(!value||!value.startsWith(config.prefix))return;
  active=true;const m=value.match(/tick=(\d+)/);if(m)tick=+m[1];
  row('marker',{value,units:Array.from(watched.values(),unit)});
  if(value.includes(' label=complete'))active=false;
 }});
 Interceptor.attach(base.add(0x693d50),{onEnter(){
  this.previous=refreshed;this.u=this.context.ecx;refreshed=this.u;this.live=active;
  if(this.live)row('refresh-begin',{unit:unit(this.u),caller:rva(this.returnAddress)});
 },onLeave(){if(this.live)row('refresh-end',{unit:unit(this.u)});refreshed=this.previous;}});
 Interceptor.attach(base.add(0x1710e0),{onEnter(){
  this.m=this.context.ecx;this.u=refreshed;this.live=active;
  if(this.u)movers.set(this.u.toString(),this.m);
  this.args=Array.from({length:4},(_,i)=>this.context.esp.add(4+i*4).readU32());
  if(this.live)row('configure-begin',{unit:this.u?unit(this.u):null,args:this.args});
 },onLeave(){if(this.live)row('configure-end',{unit:this.u?unit(this.u):null,args:this.args,sep:sep(this.m)});}});
 for(const [offset,event] of [[0x694690,'remove'],[0x688d90,'acquire'],[0x6785c0,'release'],[0x69c510,'inactive'],[0x698ce0,'owner']]) {
  Interceptor.attach(base.add(offset),{onEnter(){
   this.u=this.context.ecx;this.live=active;if(!this.live)return;
   if(event==='remove')watched.set(this.u.toString(),this.u);
   row(event+'-begin',{unit:unit(this.u),caller:rva(this.returnAddress)});
  },onLeave(){if(this.live)row(event+'-end',{unit:unit(this.u)});}});
 }
 Interceptor.attach(base.add(0x16eb20),{onEnter(){
  this.m=this.context.ecx;this.key=null;
  for(const [key,m] of movers)if(m.equals(this.m)){this.key=key;break;}
  if(this.key)row('destroy',{unit:unit(ptr(this.key))});
 },onLeave(){if(this.key){movers.delete(this.key);watched.delete(this.key);}}});
 emit('installed',{hooks:10});
}});
