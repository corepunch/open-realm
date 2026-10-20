// Observe a public blocked point order through recovery and queued successor dispatch.
let installed=false,active=false,recording=true,unit=null;
const counts={};
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(module.name.toLowerCase()!=='game.dll'||installed)return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Recovery197 PE mismatch');
 installed=true;
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32()>>>0);
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32()>>>0;
 const state=u=>u===null||u.isNull()?null:({id:words(u.add(0xc),2),refs:u.add(4).readU32(),control:u.add(0x5c).readU32(),tasks:words(u.add(0x174),2),orders:words(u.add(0x19c),2),tail:words(u.add(0x1a8),2),count:u.add(0x1b4).readU32()});
 const row=(event,data={})=>{if(active)emit(event,{c:counter(),unit:state(unit),...data});};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;active=true;row('marker',{value});if(value.includes(' label=complete'))active=false;}});
 Interceptor.attach(base.add(0x5fd270),{onEnter(){unit=this.context.ecx.add(0x30).readPointer();row('create-move-tasks');}});
 Interceptor.attach(base.add(0x6b93a0),{onEnter(args){row('publish-order',{flags:args[0].toUInt32()&0xffff,order:this.context.edx.add(0x24).readU32(),point:words(this.context.edx.add(0x48),1).concat(words(this.context.edx.add(0x50),1))});}});
 for(const [rva,event,ability]of [[0x603110,'cant-path',true],[0x5fb190,'recover',true],[0x5fa7a0,'arrival-cleanup',true],[0x691260,'pop-task',false],[0x67df00,'dispatch-tasks',false],[0x693490,'append-order',false]])
  Interceptor.attach(base.add(rva),{onEnter(){this.live=active;if(!this.live)return;this.p=this.context.ecx;if(ability)unit=this.p.add(0x30).readPointer();row(event+'-begin');},onLeave(ret){if(this.live)row(event+'-end');}});
 for(const [rva,event]of [[0x691f20,'prepend-event'],[0x6927a0,'prepend-action']])
  Interceptor.attach(base.add(rva),{onEnter(args){row(event,{argument:args[0].toUInt32()});}});
 Interceptor.attach(base.add(0x166e90),{onEnter(){this.live=active;this.p=this.context.ecx;if(this.live)row('fine-begin',{retry:words(this.p.add(0x94),2)});},onLeave(ret){if(this.live)row('fine-end',{retry:words(this.p.add(0x94),2),result:ret.toUInt32()});}});
 Interceptor.attach(base.add(0x16c150),{onEnter(){this.live=active;this.p=this.context.ecx;if(this.live)row('group-begin',{id:words(this.p.add(0x14),2),count:this.p.add(0x38).readU32()});},onLeave(){if(this.live)row('group-end',{count:this.p.add(0x38).readU32()});}});
 Interceptor.attach(base.add(0x170dc0),{onEnter(args){row('blocked-completion',{partial:args[0].toUInt32()});}});
 const path=p=>({counts:[p.add(0x50).readU32(),p.add(0x70).readU32()],indices:words(p.add(0x74),2),retry:words(p.add(0x94),2),flags:p.add(0x88).readU32(),links:words(p.add(0x8c),2),destination:words(p.add(0x1c),2)});
 Interceptor.attach(base.add(0x171340),{onEnter(){this.live=active;this.p=this.context.ecx;if(this.live)row('stop-begin',{group:words(this.p.add(0x9c),2),velocity:words(this.p.add(0x80),2),path:path(this.p.add(0xa8).readPointer())});},onLeave(){if(this.live)row('stop-end',{group:words(this.p.add(0x9c),2),velocity:words(this.p.add(0x80),2),path:path(this.p.add(0xa8).readPointer())});}});
 emit('installed',{hooks:15});
}});
