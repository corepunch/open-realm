// Read-only queued request readiness and physical publication. No game calls/writes.
let installed=false,recording=true,searchDepth=0,callbackDepth=0,requests=0;
const tracked=new Set();
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 const resolve=(id)=>{const reg=base.add(0xd68610).readPointer(),negative=!!(id[0]&0x80000000),index=id[0]&0x7fffffff;if(index>=reg.add(negative?0x3c:0x1c).readU32())return null;const slot=reg.add(negative?0x2c:0xc).readPointer().add(index*8);if(slot.readU32()!==0xfffffffe)return null;const obj=slot.add(4).readPointer();return obj.add(0x18).readU32()===id[1]?obj:null;};
 const state=(req)=>{const rows=[];for(let i=0;i<12;i++){const id=words(req.add(0x1c+12*i),2),m=resolve(id);if(m)rows.push({index:i,id,ready:req.add(0xac+4*i).readU32()!==0xffffffff,counter:m.add(0x98).readPointer().add(0x40).readU32(),group:words(m.add(0x9c),2)});}return rows;};
 for(const [rva,name]of [[0x169c50,'acquire'],[0x169d60,'release']])Interceptor.attach(base.add(rva),{onEnter(){this.take=recording&&tracked.has(this.context.ecx.toString());if(!this.take)return;this.req=this.context.ecx;send({event:'scope',phase:name+'-enter',request:words(this.req.add(0x14),2),rows:state(this.req)});},onLeave(){if(this.take)send({event:'scope',phase:name+'-leave',request:words(this.req.add(0x14),2),rows:state(this.req)});}});
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
