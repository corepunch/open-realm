// Read-only original setup/advance observers; no game calls or memory writes.
let installed=false,recording=true,marker='',counts={};
const emit=(event,data={})=>{if(recording)send({event,...data});};
const words=p=>[p.readU32(),p.add(4).readU32()];
function install(module) {
 if(installed || module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4 || pe.add(8).readU32()!==config.timestamp || pe.add(80).readU32()!==config.imageSize)throw Error('Target PE differs');
 installed=true;emit('module',{base:base.toString(),path:module.path});
 const hook=(rva,callbacks)=>Interceptor.attach(base.add(rva),callbacks);
 hook(0x231df0,{onEnter(args){const value=args[0].readCString();if(!value || !value.startsWith('ROUTE012 '))return;marker=value;emit('marker',{value});}});
 for(const [rva,name,sourceOffset,nearestOffset,countOffset] of [[0x14ad50,'fine-setup',0x90,0x9c,0x40],[0x164c30,'coarse-setup',0xc4,0xd0,0x6c]]) {
  hook(rva,{onEnter(args){this.system=this.context.ecx;this.before={sourceNode:this.system.add(sourceOffset).readU32(),nearest:nearestOffset===null?null:this.system.add(nearestOffset).readU32(),count:this.system.add(countOffset).readU32()};this.source=words(args[name==='fine-setup'?0:1]);this.goal=words(args[name==='fine-setup'?1:2]);},onLeave(ret){counts[name]=(counts[name]||0)+1;if(counts[name]>200)return;emit(name,{marker,system:this.system.toString(),source:this.source,goal:this.goal,result:ret.toUInt32(),before:this.before,after:{sourceNode:this.system.add(sourceOffset).readU32(),nearest:nearestOffset===null?null:this.system.add(nearestOffset).readU32(),count:this.system.add(countOffset).readU32()}});}});
 }
 hook(0x165ae0,{onEnter(args){this.path=this.context.ecx;this.source=words(args[0]);this.output=args[1];},onLeave(ret){counts.advance=(counts.advance||0)+1;if(counts.advance>400)return;emit('advance',{marker,path:this.path.toString(),source:this.source,output:words(this.output),result:ret.toUInt32(),fineCount:this.path.add(0x50).readU32(),fineIndex:this.path.add(0x74).readU32(),coarseCount:this.path.add(0x70).readU32(),coarseIndex:this.path.add(0x78).readU32(),flags:this.path.add(0x88).readU32(),retry:this.path.add(0x98).readU32()});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){emit('observer-finish',{installed,counts});recording=false;return{installed,counts};}};
