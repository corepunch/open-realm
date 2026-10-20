// Read-only unit/bridge/widget exclusions around Stop recovery; no game calls or writes.
let installed=false,recording=true,window=false,scene=-1,serial=0,depth=0,counts={},movers=[];
function emit(event,row){counts[event]=(counts[event]||0)+1;send({event,seq:serial++,scene,...row});}
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const hook=(rva,handlers)=>Interceptor.attach(base.add(rva),handlers);
 const state=mover=>{const record=mover.add(0x98).readPointer(),owner=base.add(0xd53a48).readPointer(),fine=owner.add(0x24c).readPointer();return {counter:record.isNull()?null:record.add(0x40).readU32(),mode:fine.add(0xd4).readU32()};};
 hook(0x231df0,{onEnter(args){if(!recording||args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith(config.prefix))return;scene=Number(/scene=(\d+)/.exec(value)[1]);window=value.includes('label=before');send({event:'marker',value});}});
 const unitState=unit=>{const bridge=unit.add(0x164),id=bridge.add(8).readU32(),generation=bridge.add(12).readU32();return {unit:unit.toString(),identity:[id,generation],regions:unit.add(0x34).readPointer().isNull()?0:unit.add(0x34).readPointer().add(4).readU32()};};
 hook(0x651590,{onEnter(args){this.take=recording&&window;if(!this.take)return;this.unit=this.context.ecx;this.on=args[0].toUInt32();emit('unit-toggle-enter',{on:this.on,...unitState(this.unit)});},onLeave(){if(this.take)emit('unit-toggle-exit',{on:this.on,...unitState(this.unit)});}});
 hook(0x063d10,{onEnter(args){this.take=recording&&window;if(!this.take)return;this.list=this.context.ecx;this.on=args[0].toUInt32();this.snapshot=()=>{const n=this.list.add(4).readU32(),a=this.list.add(8).readPointer(),flags=[];for(let i=0;i<n;i++){const p=a.add(i*4).readPointer();flags.push(p.isNull()?null:p.add(0x40).readU32());}return flags;};emit('widget-toggle-enter',{on:this.on,flags:this.snapshot()});},onLeave(){if(this.take)emit('widget-toggle-exit',{on:this.on,flags:this.snapshot()});}});
 hook(0x05ca50,{onEnter(args){this.take=recording&&window;if(!this.take)return;depth++;emit('stop-enter',{callback:!args[0].isNull()});},onLeave(){if(!this.take)return;emit('stop-exit',{});depth--;}});
 hook(0x05bd30,{onEnter(args){this.take=recording&&depth;this.on=args[0].toUInt32();},onLeave(value){if(this.take)emit('outer-toggle',{on:this.on,counter:value.add(0x40).readU32()});}});
 for(const [rva,name]of [[0x171340,'stop-mover'],[0x170080,'inner']])hook(rva,{onEnter(){this.take=recording&&depth;if(!this.take)return;this.mover=this.context.ecx;if(name==='stop-mover')movers.push(this.mover);emit(name+'-enter',state(this.mover));},onLeave(){if(!this.take)return;emit(name+'-exit',state(this.mover));if(name==='stop-mover')movers.pop();}});
 for(const [rva,name]of [[0x149370,'footprint'],[0x14a1e0,'placement'],[0x05c820,'commit']])hook(rva,{onEnter(){if(!recording||!depth)return;emit(name,state(movers.at(-1)));}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,depth,counts,readOnly:true};}};
