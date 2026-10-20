// Read-only canonical publication. No native calls or writes.
let installed=false,recording=true,started=false,tick=0,depth=0,scopes=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const b=m.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:b.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(4*i).readU32());
 const resolve=(id)=>{const reg=b.add(0xd68610).readPointer(),neg=!!(id[0]&0x80000000),i=id[0]&0x7fffffff;if(i>=reg.add(neg?0x3c:0x1c).readU32())return null;const slot=reg.add(neg?0x2c:0xc).readPointer().add(i*8);if(slot.readU32()!==0xfffffffe)return null;const obj=slot.add(4).readPointer();return obj.add(0x18).readU32()===id[1]?obj:null;};
 const record=(p)=>p&&!p.isNull()?{address:p.toString(),counter:p.add(0x40).readU32(),retired:p.add(0x38).readU32()===0xffffffff}:null;
 const state=(req)=>{const targetId=words(req.add(0xdc),2),target=resolve(targetId),cache=req.add(0xf4).readPointer(),rows=[];for(let i=0;i<12;i++){const id=words(req.add(0x1c+12*i),2),mover=resolve(id);if(mover)rows.push({index:i,id,ready:req.add(0xac+4*i).readU32(),record:record(mover.add(0x98).readPointer())});}return {request:words(req.add(0x14),2),flags:req.add(0x100).readU32(),targetId,target:record(target?target.add(0x98).readPointer():null),cache:record(cache),rows};};
 Interceptor.attach(b.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){started=true;const hit=/tick=(\d+)/.exec(value);if(hit)tick=Number(hit[1]);send({event:'marker',value});}}});
 Interceptor.attach(b.add(0x16bcf0),{onEnter(){this.take=recording&&started;if(this.take){this.req=this.context.ecx;this.id=words(this.req.add(0x14),2);depth++;send({event:'publish',tick,depth,...state(this.req)});}},onLeave(ret){if(this.take){send({event:'published',tick,depth,request:this.id,result:ret.toUInt32()});depth--;}}});
 for(const [rva,name]of [[0x169c50,'acquire'],[0x169d60,'release']])Interceptor.attach(b.add(rva),{onEnter(){this.take=recording&&started&&depth>0;if(this.take){this.req=this.context.ecx;scopes++;send({event:'scope',tick,phase:name+'-enter',...state(this.req)});}},onLeave(){if(this.take)send({event:'scope',tick,phase:name+'-leave',...state(this.req)});}});
 Interceptor.attach(b.add(0x16da60),{onEnter(args){this.take=recording&&started&&depth>0;if(this.take){this.req=this.context.ecx;this.on=args[0].toUInt32();send({event:'target-toggle',tick,phase:'enter',on:this.on,...state(this.req)});}},onLeave(){if(this.take)send({event:'target-toggle',tick,phase:'leave',on:this.on,...state(this.req)});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,depth,scopes};}};
