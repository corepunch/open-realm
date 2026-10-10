// Read-only queued request readiness and physical publication. No game calls/writes.
let installed=false,recording=true,searchDepth=0,callbackDepth=0,requests=0;
const tracked=new Set();
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 const ready=p=>Array.from({length:12},(_,i)=>{const v=p.add(0xac+i*4).readPointer();return v.toUInt32()===0xffffffff?null:words(v.add(0x14),2);});
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix))send({event:'marker',value});}});
 Interceptor.attach(base.add(0x5fa950),{onEnter(){this.take=recording;if(this.take)searchDepth++;},onLeave(){if(this.take)searchDepth--;}});
 Interceptor.attach(base.add(0x5faaf0),{onEnter(){this.take=recording&&searchDepth;if(!this.take)return;callbackDepth++;this.ctx=this.context.edx;this.source=this.ctx.readPointer();},onLeave(ret){if(!this.take)return;send({event:'candidate',result:ret.toUInt32(),accepted:this.ctx.add(0x20).readU32(),source:this.source.toString()});callbackDepth--;}});
 Interceptor.attach(base.add(0x89cd10),{onEnter(args){this.take=recording&&searchDepth;if(!this.take)return;send({event:'ready',callback:!!callbackDepth,mover:words(args[0].add(0x14),2),wrapper:this.context.ecx.toString()});}});
 Interceptor.attach(base.add(0x16d850),{onEnter(args){if(!recording||!tracked.has(this.context.ecx.toString()))return;send({event:'set-ready',request:words(this.context.ecx.add(0x14),2),mover:words(args[0].add(0x14),2),value:args[1].toUInt32(),callback:!!callbackDepth});}});
 Interceptor.attach(base.add(0x16bcf0),{onEnter(){this.request=this.context.ecx;this.take=recording&&(searchDepth||tracked.has(this.request.toString()));if(!this.take)return;tracked.add(this.request.toString());this.row={event:'publish',callback:!!callbackDepth,request:words(this.request.add(0x14),2),before:ready(this.request)};requests++;},onLeave(ret){if(!this.take)return;this.row.result=ret.toInt32();this.row.after=ready(this.request);send(this.row);if(this.row.result>=0)tracked.delete(this.request.toString());}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,requests,searchDepth,callbackDepth};}};
