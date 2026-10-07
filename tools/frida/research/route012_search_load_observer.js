// Read-only constructor/load observations; no native calls or memory writes.
// Separate from the full MAP-06 observer so its old captures remain interpretable.
let route012LoadInstalled=false,route012LoadGeneration=0;
function route012LoadInstall(module) {
 if(route012LoadInstalled || module.name.toLowerCase()!=='game.dll')return;
 route012LoadInstalled=true;
 const base=module.base;
 const state=(system,coarse)=>({system:system.toString(),
  source:system.add(coarse?0xc4:0x90).readU32(),nearest:system.add(coarse?0xd0:0x9c).readU32(),
  count:system.add(coarse?0x6c:0x40).readU32(),budget:system.add(coarse?0x98:0x68).readU32()});
 for(const [rva,coarse,kind] of [[0x147600,false,'construct'],[0x14f570,true,'construct'],
                               [0x1481e0,false,'load'],[0x162da0,true,'load']]) {
  Interceptor.attach(base.add(rva),{onEnter(){this.system=this.context.ecx;},onLeave(){
   send({event:'search-owner-'+kind,generation:route012LoadGeneration,coarse,...state(this.system,coarse)});
  }});
 }
 Interceptor.attach(base.add(0x15b1c0),{onEnter(){route012LoadGeneration++;},onLeave(){
  const owner=base.add(0xd53a48).readPointer();
  send({event:'search-owners-after-load',generation:route012LoadGeneration,
        fine:state(owner.add(0x24c).readPointer(),false),coarse:state(owner.add(0x250).readPointer(),true)});
 }});
}
Process.attachModuleObserver({onAdded:route012LoadInstall});
