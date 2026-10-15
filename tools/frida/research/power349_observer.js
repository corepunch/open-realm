// Read-only scalar pointer relationships through public Pow and Shadow Strike recovery.
let installed=false,active=false,finishing=false,base,tick=0,seq=0,curveDepth=0;const counts={},query=[0,0,0,0,0];
const emit=(event,data={})=>{counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,primary:base.add(0xd53a48).readPointer().isNull()?null:Array.from({length:3},(_,i)=>base.add(0xd53a48).readPointer().add(0x54+i*4).readU32()),...data});};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;installed=true;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 for(const [r,prefix]of Object.entries(config.prologues)){
  const bytes=Array.from(new Uint8Array(base.add(Number(r)).readByteArray(prefix.length/2))).map(v=>v.toString(16).padStart(2,'0')).join('');
  if(bytes!==prefix)throw Error('entry bytes differ '+r);
 }
 if(!base.add(0x231df1).readPointer().equals(base.add(0xd68a24)))throw Error('Preload relocated operand differs');
 const hook=(r,c)=>Interceptor.attach(base.add(r),c);
 hook(0x231df0,{onEnter(a){const value=a[0].readCString();if(!value||!value.startsWith(config.prefix))return;tick=Number(/tick=(\d+)/.exec(value)[1]);if(value.includes('label=start'))active=true;const m=/label=unit u=(\d+)/.exec(value);if(m)emit('public-values',{u:Number(m[1]),values:query.slice()});emit('marker',{value});if(value.includes('label=complete'))finishing=true;if(finishing&&m&&Number(m[1])===1)active=false;}});
 for(const [r,k]of [[0x204100,0],[0x204140,1],[0x203b80,2],[0x203d30,3],[0x204050,4]])hook(r,{onEnter(){this.take=active;},onLeave(ret){if(this.take)query[k]=ret.toUInt32();}});
 for(const [r,name]of [[0x0710e0,'power'],[0x070c20,'exp'],[0x06fe10,'exp-quarter'],[0x070f70,'log'],[0x06ff20,'log-reduced'],[0x06fd50,'log-core']])hook(r,{onEnter(a){this.take=active;this.out=this.context.ecx;this.src=this.context.edx;if(!this.take)return;const data={name,caller:this.returnAddress.sub(base).toString(),input:this.src.readU32(),alias:this.out.equals(this.src)};if(name==='power'){this.exponent=a[0];data.exponent=a[0].readU32();data.exponentAlias=this.out.equals(a[0]);data.inputExponentAlias=this.src.equals(a[0]);}emit('scalar-enter',data);},onLeave(){if(this.take)emit('scalar-leave',{name,result:this.out.readU32()});}});
 hook(0x502600,{onEnter(a){this.take=active;this.buff=this.context.ecx;this.out=a[0];if(this.take)curveDepth++;if(this.take)emit('curve-enter',{exponent:this.buff.add(0x134).readU32(),duration:this.buff.add(0x13c).readU32(),retained:this.buff.add(0x144).readU32(),interval:this.buff.add(0x12c).readU32(),damage:this.buff.add(0xec).readU32(),move:this.buff.add(0x108).readU32(),attack:this.buff.add(0x110).readU32()});},onLeave(){if(this.take){emit('curve-leave',{result:this.out.readU32()});curveDepth--;}}});
 hook(0x5181b0,{onEnter(a){this.take=active;this.buff=this.context.ecx;},onLeave(){if(this.take)emit('buff-applied',{interval:this.buff.add(0x12c).readU32(),damage:this.buff.add(0xec).readU32(),move:this.buff.add(0x108).readU32(),attack:this.buff.add(0x110).readU32(),duration:this.buff.add(0x13c).readU32(),exponent:this.buff.add(0x134).readU32()});}});
 hook(0x5fb7c0,{onEnter(a){this.take=active;this.move=this.context.ecx;if(this.take)emit('factor-enter',{delta:a[0].readU32(),factor:this.move.add(0x78).readU32(),caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.take)emit('factor-leave',{factor:this.move.add(0x78).readU32()});}});
 hook(0x060630,{onEnter(a){this.take=active&&curveDepth>0;this.out=a[0];},onLeave(){if(this.take)emit('curve-age',{value:this.out.readU32()});}});
 for(const [r,name]of [[0x50bcf0,'cast-effect'],[0x500a60,'missile-impact']])hook(r,{onEnter(){if(active)emit(name);}});
 hook(0x6d3470,{onEnter(a){if(active&&this.context.ecx.readPointer().equals(base.add(0xb09438)))emit('missile-source',{start:[a[0].readU32(),a[0].add(4).readU32(),a[0].add(8).readU32()],speed:a[0].add(0x4c).readU32()});}});
 hook(0x161b70,{onEnter(a){this.take=active;this.scalar=this.context.ecx;if(this.take)emit('scalar-value-write',{delta:a[0].readU32(),base:this.scalar.add(0x78).readU32(),rate:this.scalar.add(0x7c).readU32(),origin:[this.scalar.add(0x70).readU32(),this.scalar.add(0x74).readU32()],caller:this.returnAddress.sub(base).toString()});},onLeave(){if(this.take)emit('scalar-value-committed',{base:this.scalar.add(0x78).readU32(),rate:this.scalar.add(0x7c).readU32(),origin:[this.scalar.add(0x70).readU32(),this.scalar.add(0x74).readU32()]});}});
 emit('module',{base:base.toString()});
}});rpc.exports={finish(){return{installed,counts,readOnly:true};}};
