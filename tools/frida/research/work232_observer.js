// Read-only queued-cohort scope and query tokens. No writes or native calls.
let installed=false,recording=true,depth=0,searches=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix))send({event:'marker',value});}});
 Interceptor.attach(base.add(0x5fa950),{onEnter(args){
  this.take=recording;if(!this.take)return;depth++;searches++;
  this.unit=this.context.ecx;this.row={event:'cohort',source:this.unit.toString(),previous:words(this.unit.add(0x240),2),category:this.unit.add(0x1fc).readU32(),point:[this.context.edx.readU32(),args[0].readU32()],radius:base.add(0xd6fb38).readU32()};
 },onLeave(ret){if(!this.take)return;this.row.result=ret.toUInt32();send(this.row);depth--;}});
 Interceptor.attach(base.add(0x67e790),{onEnter(args){if(!depth||!recording)return;send({event:'query',words:words(this.context.esp.add(4),8)});}});
 Interceptor.attach(base.add(0x5faaf0),{onEnter(){if(!depth||!recording)return;this.ctx=this.context.edx;const unit=this.context.ecx;this.row={event:'candidate',unit:unit.toString(),previous:words(unit.add(0x240),2),category:unit.add(0x1fc).readU32(),context:words(this.ctx,10)};},onLeave(ret){if(!this.row)return;this.row.result=ret.toUInt32();this.row.accepted=this.ctx.add(0x20).readU32();send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,searches};}};
