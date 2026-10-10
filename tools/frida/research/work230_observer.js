// Read-only widget collection rectangle producer. No game calls or writes.
let installed=false,recording=true,count=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32());
 Interceptor.attach(base.add(0x642f0),{onEnter(args){
  this.collection=this.context.ecx;this.take=count++<4096;
  if(!this.take)return;
  this.bounds=words(args[0],4);this.origin=words(base.add(0xd3c82c).readPointer().add(0x6c),2);
 },onLeave(){
  if(!recording||!this.take)return;
  const n=this.collection.add(4).readU32(),a=this.collection.add(8).readPointer();
  if(n>4)throw Error('collection count differs');
  send({event:'region-bounds',bounds:this.bounds,origin:this.origin,
   boxes:Array.from({length:n},(_,i)=>{const o=a.add(4*i).readPointer();return o.isNull()?null:words(o.add(0x1c),4);})});
 }});
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix))send({event:'marker',value});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,count};}};
