// Read-only source-selection inputs, predicted eligibility and chosen row.
let installed=false,recording=true,phase=-1,count=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){const match=/ begin=(\d+)/.exec(value);if(match)phase=Number(match[1]);send({event:'marker',value});}}});
 Interceptor.attach(base.add(0x16c6d0),{onEnter(args){
  this.take=recording&&phase>=0&&count<1024;if(!this.take)return;count++;
  const group=this.context.ecx,n=group.add(0x38).readU32(),rows=group.add(0x28).readPointer();
  if(n<1||n>12)throw Error('group count differs');
  this.row={event:'source',phase,flags:group.add(0x80).readU32(),goal:words(args[0],2),members:Array.from({length:n},(_,i)=>{const mover=rows.add(i*0x2c+0x14).readPointer(),path=mover.add(0xa8).readPointer();return{position:words(mover.add(0x78),2),velocity:words(mover.add(0x80),2),clock:words(mover.add(0x70),2),flags:path.add(0x88).readU32()};})};
 },onLeave(ret){if(!this.take)return;this.row.index=ret.toUInt32();send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,count};}};
