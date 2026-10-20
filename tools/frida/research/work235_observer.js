// Read-only recursive cohort construction; no calls, writes or substitutions.
let installed=false,recording=true,phase=-1,count=0,depth=0;
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 function members(group){let n=group.add(0x38).readU32(),rows=group.add(0x28).readPointer();if(n>12)throw Error('members differ');return Array.from({length:n},(_,i)=>words(rows.add(i*44),2));}
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){const match=/ begin=(\d+)/.exec(value);if(match)phase=Number(match[1]);send({event:'marker',value});}}});
 Interceptor.attach(base.add(0x16b7b0),{onEnter(args){
  this.take=recording&&phase>=0&&count<1024;if(!this.take)return;count++;
  const req=this.context.ecx;this.group=args[0];this.bindDepth=depth++;
  let candidates=Array.from({length:12},(_,i)=>req.add(0xac+i*4).readPointer());
  this.row={event:'bind',phase,depth:this.bindDepth,index:args[1].toUInt32(),flags:req.add(0x100).readU32(),
   candidates:candidates.map(p=>p.isNull()||p.toUInt32()===0xffffffff?null:{identity:words(p.add(0x14),2),pose:words(p.add(0x78),4),path:p.add(0xa8).readPointer().add(0x88).readU32()})};
 },onLeave(){if(!this.take)return;if(--depth!==this.bindDepth)throw Error('unbalanced bind');this.row.members=members(this.group);send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,count,depth};}};
