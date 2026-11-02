// Read-only TARGET-04.1: request flag provenance and owner-counter refresh cadence.
let installed=false,recording=true,started=false;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const b=m.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:b.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32());
 const counter=()=>b.add(0xd53a48).readPointer().add(0x538).readU32();
 const resolve=(id)=>{const reg=b.add(0xd68610).readPointer(),neg=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;if(i>=reg.add(neg?0x3c:0x1c).readU32())return null;const slot=reg.add(neg?0x2c:0xc).readPointer().add(i*8);if(slot.readU32()!==0xfffffffe)return null;const obj=slot.add(4).readPointer();return obj.add(0x18).readU32()===id[1]?obj:null;};
 const snap=g=>{const p=g.add(0x3c).readPointer();return {id:words(g.add(0x14),2),target:words(g.add(0x40),2),flags:g.add(0x80).readU32(),cd:g.add(0x64).readS32(),destination:p.isNull()?null:words(p.add(0x1c),2),times:p.isNull()?null:words(p.add(0x7c),2)};};
 const hook=(a,c)=>Interceptor.attach(b.add(a),c);
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){started=true;send({event:'marker',c:counter(),value});}}});
 hook(0x89cca0,{onEnter(args){if(!recording||!started)return;const req=resolve(words(this.context.ecx.add(8),2));if(req)send({event:'extra-producer',c:counter(),caller:this.returnAddress.sub(b).toUInt32(),request:words(req.add(0x14),2),flags:req.add(0x100).readU32(),enable:args[0].toUInt32()});}});
 hook(0x16bdb0,{onEnter(args){this.take=recording&&started;if(this.take){this.req=this.context.ecx;this.row={event:'handoff',c:counter(),request:words(this.req.add(0x14),2),flags:this.req.add(0x100).readU32(),target:words(this.req.add(0xdc),2)};}},onLeave(ret){if(this.take)send({...this.row,result:ret.toString()});}});
 hook(0x16c150,{onEnter(){this.take=recording&&started;this.g=this.context.ecx;if(this.take)send({event:'owner',c:counter(),...snap(this.g)});}});
 hook(0x16cd30,{onEnter(args){this.take=recording&&started;this.g=this.context.ecx;this.out=args[0];if(this.take)this.before=snap(this.g);},onLeave(){if(this.take)send({event:'sample',c:counter(),before:this.before,...snap(this.g),sample:words(this.out,2)});}});
 const pending=new Map();
 hook(0x169680,{onEnter(){this.g=this.context.ecx;this.take=recording&&started;if(this.take){this.before=snap(this.g);pending.set(this.g.toString(),{});}},onLeave(){if(this.take){const k=this.g.toString();send({event:'refresh',c:counter(),before:this.before,...snap(this.g),...pending.get(k)});pending.delete(k);}}});
 hook(0x169727,{onEnter(){const r=pending.get(this.context.ebx.toString());if(r)r.distance=this.context.ebp.sub(8).readU32();}});
 hook(0x16974d,{onEnter(){const r=pending.get(this.context.ebx.toString());if(r)r.candidate=this.context.eax.toInt32();}});
 hook(0x16ce10,{onEnter(args){this.take=recording&&started;this.g=this.context.ecx;this.ready=args[2];this.final=args[3];this.in=words(args[0],2);},onLeave(ret){if(this.take)send({event:'request',c:counter(),...snap(this.g),input:this.in,result:ret.toUInt32(),ready:this.ready.readU32(),final:this.final.readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed};}};
