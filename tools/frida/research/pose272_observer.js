// Read-only MOVE-03.3 native published orientation and support samples.
let installed=false,recording=true,seq=0,depth=0,count=0,gridSeen=false;
const active=new Map(),emit=(event,data={})=>{if(recording)send({event,seq:++seq,...data});};
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const b=m.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;const at=r=>b.add(r),words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 emit('module');
 Interceptor.attach(at(0x66d990),{onEnter(args){
  this.rec=null;const unit=this.context.ecx,code=unit.add(0x30).readU32();
  if(![0x68666f6f,0x68503732,0x685a3732].includes(code))return;
  if(!gridSeen){const t=at(0xd726c0).readPointer(),g=t.add(0x79c).readPointer(),n=g.readU32()*g.add(4).readU32();if(n>4096)throw Error('unexpected grid size');emit('grid',{shape:words(g,4),origin:words(t.add(0xc4),2),values:words(g.add(0x18).readPointer(),n)});gridSeen=true;}
  this.rec={type:code,flags:unit.add(0x5c).readU32(),point:words(args[1],3),caller:this.returnAddress.sub(b).toUInt32(),samples:[],decks:[]};
  this.out=args[0];active.set(Process.getCurrentThreadId(),this.rec);depth++;count++;
 },onLeave(result){if(!this.rec)return;this.rec.matrix=words(this.out,9);emit('pose',this.rec);active.delete(Process.getCurrentThreadId());depth--;}});
 Interceptor.attach(at(0x66da38),function(){const r=active.get(Process.getCurrentThreadId());if(r)r.inputs={heading:this.context.esp.add(8).readU32(),pitch:this.context.esp.add(0x5c).readU32(),roll:this.context.esp.add(0x4c).readU32()};});
 Interceptor.attach(at(0x66dd5e),function(){const r=active.get(Process.getCurrentThreadId());if(r)r.samples.push({point:words(this.context.esi,2),height:this.context.esp.add(0x38).readU32()});});
 Interceptor.attach(at(0x1c7ba0),{onEnter(args){this.r=active.get(Process.getCurrentThreadId());if(this.r){this.out=[args[0],args[1],args[2]];this.r.unclampedMatrix=words(this.context.ecx,9);}},onLeave(result){if(this.r)this.r.unclamped=this.out.map(p=>p.readU32());}});
 Interceptor.attach(at(0x1c6c40),{onEnter(args){const r=active.get(Process.getCurrentThreadId());if(r)r.angles=[args[0].toUInt32(),args[1].toUInt32(),args[2].toUInt32()];}});
 Interceptor.attach(at(0x782a80),{onEnter(args){this.r=active.get(Process.getCurrentThreadId());if(this.r){this.point=words(args[0],2);this.out=args[1];}},onLeave(result){if(this.r){const d={point:this.point,hit:result.toInt32()};if(d.hit)d.height=this.out.readU32();this.r.decks.push(d);}}});
 Interceptor.attach(at(0x684480),{onEnter(args){this.unit=this.context.ecx;this.code=this.unit.add(0x30).readU32();this.keep=[0x68666f6f,0x68503732,0x685a3732].includes(this.code);},onLeave(result){if(this.keep)emit('support',{type:this.code,flags:this.unit.add(0x280).readU32(),point:words(this.unit.add(0x284),3),heading:this.unit.add(0x290).readU32(),matrix:words(this.unit.add(0x294),9)});}});
 Interceptor.attach(at(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(value.startsWith(config.prefix))emit('marker',{value});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,readOnly:true,depth,count};}};
