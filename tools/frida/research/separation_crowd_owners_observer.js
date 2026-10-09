// Read-only physical owners and member commits. The base separation observer records tick/identity.
let crowdOwnersInstalled=false;
Process.attachModuleObserver({onAdded(module) {
 if(crowdOwnersInstalled || module.name.toLowerCase()!=='game.dll')return;
 crowdOwnersInstalled=true;
 const base=module.base, words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 const counter=()=>base.add(0xd53a48).readPointer().add(0x538).readU32();
 const snapshot=group=>{
  const count=group.add(0x38).readU32(),data=group.add(0x28).readPointer();
  if(count>12)throw new Error('Invalid crowd owner extent');
  return {group:group.toString(),identity:words(group.add(0x14),2),flags:group.add(0x80).readU32(),
   raw48:words(group.add(0x48),3),
   members:Array.from({length:count},(_,i)=>{const p=data.add(i*0x2c),m=p.add(0x14).readPointer();
    return {mover:m.toString(),row:words(p,11),pose:m.isNull()?null:words(m.add(0x70),8),requested:m.isNull()?null:words(m.add(0xc0),2)};})};
 };
 Interceptor.attach(base.add(0x1689d0),{onEnter(args){
  const p=this.context.ecx,m=base.add(0xd53a8c).readPointer();
  send({event:'crowd-retry-input',counter:counter(),mover:m.toString(),source:words(args[0],2),
        adjusted:words(p.add(0x24),2),coarseCount:p.add(0x70).readU32(),coarseIndex:p.add(0x78).readU32(),
        coarse:p.add(0x60).readPointer().isNull()?null:words(p.add(0x60).readPointer(),2)});
 }});
 Interceptor.attach(base.add(0x16c150),{onEnter(){this.group=this.context.ecx;this.before=snapshot(this.group);},
  onLeave(){send({event:'crowd-physical-owner',counter:counter(),before:this.before,after:snapshot(this.group)});}});
 Interceptor.attach(base.add(0x16ce10),{onEnter(){this.group=this.context.ecx;this.before=snapshot(this.group);},
  onLeave(result){send({event:'crowd-coarse-admission',counter:counter(),result:result.toUInt32(),before:this.before,after:snapshot(this.group)});}});
}});
