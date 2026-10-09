// Read-only function-boundary observer: no original calls, no writes, no interior hooks.
let installed=false,recording=true,tick=0;const counts={},units=new Map();
const emit=(event,data={})=>{if(recording)send({event,...data});};
const bump=k=>counts[k]=(counts[k]||0)+1;
function install(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('wrong PE');
 installed=true;const at=r=>base.add(r),hook=(r,c)=>Interceptor.attach(at(r),c),u32=p=>p.readU32(),counter=a=>[0,1,2].map(i=>a.add(0x224+i*4).readS32());
 const watch=u=>{if(u.isNull())return;const code=u32(u.add(0x30));if([0x68524130,0x68524131,0x68524132].includes(code))units.set(code,ptr(u.toString()));};
 const snapshot=()=>{for(const [code,u] of units){const a=u.add(0x1e8).readPointer();emit('state',{tick,code,attack:!a.isNull(),counts:a.isNull()?null:counter(a),spells:u.add(0x1d0).readS32()});}};
 hook(0x1eef90,{onLeave(ret){watch(ret);}});
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(!value.startsWith('RSG '))return;tick=Number(/tick=(\d+)/.exec(value)[1]);emit('marker',{value});snapshot();}});
 hook(0x497da0,{onEnter(args){this.a=this.context.ecx;this.row={tick,release:args[0].toUInt32(),masks:[1,2,3].map(i=>args[i].toUInt32()),before:counter(this.a)};},onLeave(){bump('suppression');emit('suppression',{...this.row,after:counter(this.a)});}});
 hook(0x48f380,{onEnter(args){this.u=this.context.ecx;this.row={tick,enable:args[0].toUInt32(),before:this.u.add(0x1d0).readS32()};},onLeave(){bump('spells');emit('spells',{...this.row,after:this.u.add(0x1d0).readS32()});}});
 for(const [name,rva] of [['apply',0x501ae0],['remove',0x521550]])hook(rva,{onEnter(){const b=this.context.ecx;bump(name);emit('buff',{tick,name,mask:u32(b.add(0xf0)),flags:u32(b.add(0x20))});}});
}
Process.attachModuleObserver({onAdded:install});rpc.exports={status(){return{installed,counts};},finish(){recording=false;return{installed,counts};}};
