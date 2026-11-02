// Read-only UI request affinity and recursive physical group membership.
let installed=false,recording=true,started=false,dispatch=0,count=0,depth=0,tick=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){started=true;const hit=/tick=(\d+)/.exec(value);if(hit)tick=Number(hit[1]);send({event:'marker',value});}}});
 Interceptor.attach(base.add(0x6b9f70),{onEnter(){this.take=recording&&started;if(!this.take)return;dispatch++;send({event:'packet',words:words(this.context.ecx,12)});},onLeave(){if(this.take)dispatch--;}});
 let scoreDepth=0;
 Interceptor.attach(base.add(0x687b30),{onEnter(args){this.take=recording&&dispatch;if(!this.take)return;scoreDepth++;this.row={event:'score',unit:this.context.ecx.toString(),order:args[0].toUInt32(),point:[args[1].readU32(),args[2].readU32()]};},onLeave(value){if(!this.take)return;scoreDepth--;this.row.value=value.toUInt32();send(this.row);}});
 Interceptor.attach(base.add(0x058900),{onEnter(args){this.take=recording&&scoreDepth;if(this.take)this.out=args[0];},onLeave(){if(this.take)send({event:'score-pose',words:words(this.out,3)});}});
 Interceptor.attach(base.add(0x6ba800),{onEnter(){this.take=recording&&dispatch;if(!this.take)return;this.ctx=this.context.edx;this.old=this.ctx.add(0x38).readU32();},onLeave(){if(!this.take)return;const n=this.ctx.add(0x38).readU32();if(n===this.old)return;if(n!==this.old+1||n>12)throw Error('candidate count');const row=words(this.ctx.add(0x3c+this.old*36),9),u=ptr(row[0]);send({event:'candidate',unit:u.toString(),row,unit198:u.add(0x198).readU32(),unit1b4:u.add(0x1b4).readU32(),canonical:u.add(0xc).readU32()});}});
 Interceptor.attach(base.add(0x6b93a0),{onEnter(args){if(recording&&dispatch)send({event:'publish',unit:this.context.ecx.toString(),flags:args[0].toUInt32()});}});
 Interceptor.attach(base.add(0x6b8c10),{onEnter(){if(!recording||!dispatch)return;const u=this.context.ecx,ctx=this.context.edx;send({event:'attach',unit:u.toString(),type:u.add(0x1fc).readU32(),flags:u.add(0x5c).readU32(),forced:u.add(0x200).readU32(),mover:words(u.add(0x16c),2),requests:words(ctx,3),options:ctx.add(0x18).readU32()});}});
 Interceptor.attach(base.add(0x89c7c0),{onEnter(args){if(!recording||!dispatch)return;send({event:'retain',unit:args[0].sub(0x164).toString(),wrapper:this.context.ecx.toString(),request:words(this.context.ecx.add(8),2)});}});
 Interceptor.attach(base.add(0x89cd10),{onEnter(args){this.take=recording&&started;if(!this.take)return;this.wrapper=this.context.ecx;send({event:'ready',tick,wrapper:this.wrapper.toString(),request:words(this.wrapper.add(8),2),mover:words(args[0].add(0x14),2)});}});
 Interceptor.attach(base.add(0x16bcf0),{onEnter(){this.take=recording&&started;if(!this.take)return;this.req=this.context.ecx;send({event:'publish-ready',tick,request:words(this.req.add(0x14),2),flags:this.req.add(0x100).readU32(),slots:words(this.req.add(0xac),12)});},onLeave(value){if(this.take)send({event:'publish-ready-end',tick,request:words(this.req.add(0x14),2),result:value.toUInt32()});}});
 Interceptor.attach(base.add(0x16b7b0),{onEnter(args){this.take=recording&&started&&count<1024;if(!this.take)return;count++;this.group=args[0];this.bindDepth=depth++;const req=this.context.ecx;this.row={event:'bind',tick,depth:this.bindDepth,request:words(req.add(0x14),2),index:args[1].toUInt32(),flags:req.add(0x100).readU32()};},onLeave(){if(!this.take)return;if(--depth!==this.bindDepth)throw Error('unbalanced');const n=this.group.add(0x38).readU32(),rows=this.group.add(0x28).readPointer();if(n>12)throw Error('count');this.row.members=Array.from({length:n},(_,i)=>words(rows.add(i*44),2));send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,dispatch,count,depth};}};
