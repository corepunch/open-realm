// Read-only function-entry owner phases; no game calls, patches or mid-function hooks.
let installed=false,recording=true,active=false,owner=null;
const counts={};
const bump=k=>counts[k]=(counts[k]||0)+1;
const emit=(event,data={})=>{if(recording)send({event,...data});};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Schedule190 PE mismatch');
 if(installed)return;installed=true;
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32()>>>0);
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32()>>>0;
 const row=(event,data={})=>{if(active){bump(event);emit(event,{c:counter(),...data});}};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  active=true;bump('marker');emit('marker',{value});
  if(value.includes(' label=complete'))active=false;
 }});
 Interceptor.attach(base.add(0x15aa80),{onEnter(){owner=this.context.ecx;row('owner-begin');},onLeave(){row('owner-end');owner=null;}});
 for(const [offset,event] of [[0x167310,'scheduler'],[0x16c220,'publish'],[0x16e1f0,'radius'],[0x16c150,'group'],[0x16c250,'decide'],[0x16c570,'commit'],[0x1705c0,'settle'],[0x1702f0,'separate']]){
  Interceptor.attach(base.add(offset),{onEnter(){
   if(!active||owner===null)return;const p=this.context.ecx;
   const data={};
   if(['publish','radius','group','decide','commit'].includes(event))data.id=words(p.add(0x14),2);
   if(['radius','group','decide','commit'].includes(event)){
    const shared=p.add(0x7c).readPointer();data.count=p.add(0x38).readU32();
    data.shared=shared.isNull()?null:words(shared.add(0x14),2);
    data.parameters=shared.isNull()?null:words(shared.add(0x20),3);
   }
   row(event,data);
  }});
 }
 emit('installed',{hooks:10});
}});
