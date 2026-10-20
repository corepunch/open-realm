// Read-only original AI location records and Captain creation/reset boundaries.
let installed=false,recording=true;
const counts={};
const emit=(event,data={})=>{if(recording)send({event,ms:Date.now(),...data});};
const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32()>>>0);
function install(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const b=module.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('unsupported PE');
 installed=true;emit('module',{base:b.toString(),path:module.path});
 const at=r=>b.add(r),hook=(r,cb)=>Interceptor.attach(at(r),cb),rel=p=>p.sub(b).toUInt32();
 const clock=()=>words(at(0xd53a48).readPointer().add(0x538),1)[0];
 const location=p=>({pointer:p.toString(),index:p.add(0x24).readU32(),attack:[p.add(0x2c).readU32(),p.add(0x34).readU32()],defense:[p.add(0x3c).readU32(),p.add(0x44).readU32()],anchors:[words(p.add(0x48),2),words(p.add(0x54),2),words(p.add(0x60),2)]});
 const locations=()=>{let n=at(0xd77e58).readU32();if(n>32)throw new Error('location pool unexpectedly large');let a=at(0xd77e74).readPointer();return Array.from({length:n},(_,i)=>location(a.add(i*4).readPointer()));};
 const owner=p=>({pointer:p.toString(),player:p.add(0x24).readU32(),locations:words(p.add(0x1b8),4),attack:words(p.add(0x2ac),2),defense:words(p.add(0x2b8),2)});
 const mover=bridge=>{let id=bridge.add(8).readU32(),epoch=bridge.add(12).readU32();if(id===0xffffffff)return null;let registry=at(0xd68610).readPointer(),other=(id&0x80000000)!==0,index=id&0x7fffffff,limit=registry.add(other?0x3c:0x1c).readU32();if(index>=limit)throw new Error('mover index');let slot=registry.add(other?0x2c:0xc).readPointer().add(index*8);if(slot.readS32()!==-2)throw new Error('dead mover');let p=slot.add(4).readPointer();if(p.add(0x18).readU32()!==epoch)throw new Error('mover generation');return {pointer:p.toString(),position:words(p.add(0x78),2),velocity:words(p.add(0x80),2),group:words(p.add(0x9c),2),flags:p.add(0xd8).readU32()};};
 const captain=p=>({pointer:p.toString(),state:p.add(0x64).readU32(),player:p.add(0x68).readU32(),flags:p.add(0x6c).readU32(),counts:words(p.add(0xb8),6),home:[p.add(0xfc).readU32(),p.add(0x104).readU32()],request:[p.add(0xd4).readU32(),p.add(0xdc).readU32()],range:p.add(0xe4).readU32(),actor:mover(p.add(0x44))});
 const owners=new Map(),captains=new Map();let refreshing=0,resetting=0;
 const snapshot=()=>({owners:[...owners.values()].map(owner),locations:locations(),captains:[...captains.values()].map(captain)});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;let value=args[0].readCString();if(!value.startsWith('R291'))return;emit('marker',{value,counter:clock()});if(!value.includes('label=sample'))emit('snapshot',{value,counter:clock(),...snapshot()});}});
 hook(0x9c5360,{onEnter(){this.p=this.context.ecx;captains.clear();owners.set(this.p.toString(),this.p);emit('create-enter',{counter:clock(),owner:owner(this.p),locations:locations(),offset:at(0xd3c7d8).readU32()});},onLeave(){emit('create-leave',{counter:clock(),owner:owner(this.p),locations:locations()});}});
 hook(0x9c5270,{onEnter(args){this.player=args[0].toUInt32();this.type=args[1].toUInt32();this.xy=[args[2].readU32(),args[3].readU32()];},onLeave(ret){if(ret.isNull())throw new Error('captain creation failed');const stable=ptr(ret.toString());captains.set(stable.toString(),stable);emit('actor-create',{counter:clock(),player:this.player,type:this.type,xy:this.xy,captain:captain(stable)});}});
 hook(0x9c01f0,{onEnter(args){this.p=this.context.ecx;this.unit=args[0].toString();this.xyz=args[1].isNull()?null:words(args[1],3);owners.set(this.p.toString(),this.p);emit('town-admit-enter',{counter:clock(),unit:this.unit,xyz:this.xyz,owner:owner(this.p),locations:locations()});},onLeave(){emit('town-admit-leave',{counter:clock(),unit:this.unit,owner:owner(this.p),locations:locations()});}});
 hook(0x9bc1c0,{onEnter(args){this.p=this.context.ecx;this.index=args[0].toUInt32();this.xy=[args[1].readU32(),args[2].readU32()];},onLeave(){emit('location-init',{counter:clock(),index:this.index,xy:this.xy,location:location(this.p)});}});
 hook(0x9bfe80,{onEnter(){this.p=this.context.ecx;this.before=location(this.p);refreshing++;},onLeave(){emit('location-refresh',{counter:clock(),reset:resetting,before:this.before,after:location(this.p)});refreshing--;}});
 hook(0x058900,{onEnter(args){if(!refreshing)return;this.out=args[0];this.bridge=this.context.ecx;},onLeave(){if(this.out)emit('anchor-position',{counter:clock(),bridge:this.bridge.toString(),xyz:words(this.out,3)});}});
 hook(0x3612d0,{onEnter(args){if(!refreshing)return;this.out=args[0];this.xyz=words(this.context.ecx,3);},onLeave(){if(this.out)emit('anchor-length',{counter:clock(),xyz:this.xyz,length:this.out.readU32()});}});
 hook(0x9c9c10,{onEnter(){this.p=this.context.ecx;owners.set(this.p.toString(),this.p);resetting++;emit('reset-enter',{counter:clock(),owner:owner(this.p),locations:locations(),captains:[...captains.values()].map(captain)});},onLeave(){emit('reset-leave',{counter:clock(),owner:owner(this.p),locations:locations(),captains:[...captains.values()].map(captain)});resetting--;}});
 hook(0x9d7ac0,{onEnter(args){this.p=this.context.ecx;this.xy=[args[0].readU32(),args[1].readU32()];captains.set(this.p.toString(),this.p);emit('teleport-enter',{counter:clock(),reset:resetting,xy:this.xy,before:captain(this.p)});},onLeave(){emit('teleport-leave',{counter:clock(),reset:resetting,xy:this.xy,after:captain(this.p)});}});
 hook(0x9d5c70,{onEnter(args){this.p=this.context.ecx;captains.set(this.p.toString(),this.p);this.xy=[args[0].readU32(),args[1].readU32()];emit('home-enter',{counter:clock(),xy:this.xy,before:captain(this.p)});},onLeave(){emit('home-leave',{counter:clock(),xy:this.xy,after:captain(this.p)});}});
 emit('constants',{offset:at(0xd3c7d8).readU32()});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={status(){return {installed,counts};},finish(){recording=false;return {installed,counts,complete:true};}};
