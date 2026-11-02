// Read-only original world point-query boundary; no writes or game calls.
let installed=false,recording=true,count=0,active=false;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){active=value.includes(' begin=');send({event:'marker',value});}}});
 Interceptor.attach(base.add(0x4df50),{onEnter(args){
  this.take=recording&&active&&count++<4096;if(!this.take)return;
  const owner=base.add(0xd53a48).readPointer();this.fine=owner.add(0x24c).readPointer();this.map=this.fine.add(0x1c).readPointer();
  this.row={event:'point',point:[this.context.ecx.readU32(),this.context.edx.readU32()],mask:args[0].readU32(),excluded:!args[1].isNull(),mode:this.fine.add(0xd4).readU32(),stamp:this.map.add(0xb4).readU32()};
 },onLeave(ret){if(!this.take)return;this.row.result=ret.toUInt32();this.row.mode_after=this.fine.add(0xd4).readU32();this.row.stamp_after=this.map.add(0xb4).readU32();send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,count};}};
