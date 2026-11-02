// Read-only function boundaries. Track owner lifetime and actual completion callbacks.
let installed=false,active=false,recording=true,owner=null,group=null;
const counts={};const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,...data});}};
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(module.name.toLowerCase()!=='game.dll'||installed)return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Completion194 PE mismatch');
 installed=true;
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32()>>>0);
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32()>>>0;
 const state=p=>({id:words(p.add(0x14),2),count:p.add(0x38).readU32(),flags:p.add(0x80).readU32(),target:words(p.add(0x40),2)});
 const groups=()=>{let p=base.add(0xd53a48).readPointer().add(0x3b8).readPointer(),out=[];while(!p.isNull()){if(out.length>100)throw new Error('group list cycle');out.push(state(p));p=p.add(8).readPointer();}return out;};
 const row=(event,data={})=>{if(active)emit(event,{c:counter(),insideOwner:owner!==null,insideGroup:group!==null,...data});};
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;active=true;row('marker',{value,groups:groups()});if(value.includes(' label=complete'))active=false;}});
 Interceptor.attach(base.add(0x15aa80),{onEnter(){owner=this.context.ecx;row('owner-begin',{groups:groups()});},onLeave(){row('owner-end',{groups:groups()});owner=null;}});
 Interceptor.attach(base.add(0x16c150),{onEnter(){this.p=this.context.ecx;group=this.p;row('group-begin',{group:state(this.p)});},onLeave(){row('group-end',{id:words(this.p.add(0x14),2),groups:groups()});group=null;}});
 Interceptor.attach(base.add(0x16c390),{onEnter(){this.p=this.context.ecx;row('completion-begin',{group:state(this.p)});},onLeave(){row('completion-end',{group:state(this.p),groups:groups()});}});
 Interceptor.attach(base.add(0x16d4e0),{onEnter(args){this.p=args[0];row('finish-member',{member:words(this.p,11)});},onLeave(){row('finished-member',{member:words(this.p,11)});}});
 emit('installed',{hooks:5});
}});
