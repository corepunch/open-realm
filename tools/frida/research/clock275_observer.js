// Read-only SCHED-01.1 clock/speed source observation. No native calls or target writes.
let installed=false,recording=true,started=false,primary=0,presentation=0;
const selectors=[0,0],inputs={},batches=[];
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const b=m.base,pe=b.add(b.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:b.toString(),path:m.path});
 const hook=(r,c)=>Interceptor.attach(b.add(r),c);
 const owner=()=>b.add(0xd53a48).readPointer();
 const clock=p=>({time:p.add(0x40).readU32(),epoch:p.add(0x44).readU32(),span:p.add(0x48).readU32(),flags:p.add(0x4c).readU32()});
 const clocks=()=>{const o=owner(),c=b.add(0xd3c82c).readPointer();return {primary:clock(o.add(0x14)),presentation:clock(o.add(0x68)),gates:[c.add(0x38).readU32(),c.add(0x3c).readU32()],counter:o.add(0x538).readU32()};};
 hook(0x231df0,{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){started=true;send({event:'marker',wall:Date.now(),value,clocks:clocks(),primary,presentation,selectors:selectors.slice(),inputs:{...inputs}});}}});
 hook(0x213350,{onEnter(args){this.take=recording&&started;this.input=args[0].toUInt32();},onLeave(){if(this.take){const settings=b.add(0xd687a8).readPointer().add(0x30).readPointer();send({event:'speed',input:this.input,canonical:settings.add(0x47).readU8(),clocks:clocks()});}}});
 hook(0x32ce50,{onEnter(){this.take=recording;this.input=this.context.ecx.toUInt32();},onLeave(){if(this.take)send({event:'speed-source',input:this.input});}});
 hook(0x3719c0,{onEnter(args){this.take=recording&&started;this.pause=args[0].toUInt32();},onLeave(){if(this.take)send({event:'pause',pause:this.pause,wall:Date.now(),clocks:clocks(),primary,presentation});}});
 hook(0x32d670,{onEnter(args){this.take=recording&&started;if(this.take){this.n=this.context.ecx;this.row={wall:Date.now(),rate:this.n.add(0x22a8).readU32(),state:this.n.add(0x278).readU32(),clock:args[0].toUInt32(),before:primary};}},onLeave(){if(this.take)send({event:'host',...this.row,after:primary});}});
 setInterval(()=>{if(recording&&started)send({event:'heartbeat',wall:Date.now(),clocks:clocks(),primary,presentation});},100);
 hook(0x36aba0,{onEnter(){if(!recording||!started)return;primary++;const k=this.context.ecx.add(0x14).readU32();inputs[k]=(inputs[k]||0)+1;}});
 hook(0x36ab40,{onEnter(){if(recording&&started)presentation++;}});
 hook(0x15c650,{onEnter(args){this.take=recording&&started;this.domain=args[0].add(0x14).readU32()>>>31;this.expected=this.context.ecx.add(this.domain?0x68:0x14);},onLeave(ret){if(this.take){if(!ret.equals(this.expected))throw Error('selector mismatch');selectors[this.domain]++;}}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,primary,presentation,selectors,inputs};}};
