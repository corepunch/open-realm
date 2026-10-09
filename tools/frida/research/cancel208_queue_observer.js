// Read-only public cancellation brackets, complete player FIFOs and search depth.
let installed=false,active=false,recording=true,tick=0,base=null,searchDepth=0;
const counts={},actors=[],byUnit=new Map();
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,tick,...data});}};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const resolve=(id,offset=0x14)=>{
 const registry=base.add(0xd68610).readPointer(),alt=(id[0]&0x80000000)!==0,index=id[0]&0x7fffffff;
 if(index>=registry.add(alt?0x3c:0x1c).readU32())return ptr(0);
 const slot=registry.add(alt?0x2c:0xc).readPointer().add(index*8);
 if(slot.readS32()!==-2)return ptr(0);const p=slot.add(4).readPointer();
 return p.isNull()||p.add(offset).readU32()!==id[0]||p.add(offset+4).readU32()!==id[1]?ptr(0):p;
};
const snapshot=a=>{
 const m=resolve(a.moverId),p=resolve(a.pathId),g=resolve(pair(m.add(0x9c)));
 if(!g.isNull()&&!a.groupId){a.groupId=pair(g.add(0x14));a.groupPathId=pair(g.add(0x3c).readPointer().add(0x14));}
 return {actor:a.role,unit:a.unit.toString(),mover:m.toString(),path:p.toString(),
  orderCount:a.unit.add(0x1b4).readU32(),orderHead:pair(a.unit.add(0x19c)),taskHead:pair(a.unit.add(0x174)),
  position:pair(m.add(0x78)),velocity:pair(m.add(0x80)),facing:m.add(0x8c).readU32(),
  groupIdentity:pair(m.add(0x9c)),groupMembers:g.isNull()?0:g.add(0x38).readU32(),
  originalGroupLive:!!a.groupId&&!resolve(a.groupId).isNull(),
  originalGroupPathLive:!!a.groupPathId&&!resolve(a.groupPathId).isNull(),
  counts:[p.add(0x50).readU32(),p.add(0x70).readU32()],indices:pair(p.add(0x74)),
  flags:p.add(0x88).readU32(),links:pair(p.add(0x8c)),destination:pair(p.add(0x1c)),
  delay:p.add(0x94).readU32(),retry:p.add(0x98).readU32()};
};
const buckets=()=>{
 const rows=[],owner=base.add(0xd53a48).readPointer();
 for(let i=0;i<8;i++){
  const b=base.add(0xd53a90+i*0x1c),n=b.add(0x10).readU32(),q=[];
  if(n>512)throw new Error('unexpected FIFO size');let p=b.add(0x14).readPointer();
  for(let j=0;j<n;j++){
   if(p.isNull()||p.equals(ptr('0xffffffff')))throw new Error('truncated FIFO');
   q.push(p.toString());p=p.add(0x90).readPointer();
  }
  if(n&&!p.equals(ptr('0xffffffff')))throw new Error('unexpected FIFO tail');
  rows.push({offset:i*0x1c,counter:owner.add(0x538).readU32(),clock:pair(owner.add(0x54)),
   limit:b.add(4).readU32(),work:b.add(8).readU32(),countdown:b.add(12).readU32(),
   queue:q,head:b.add(0x14).readPointer().toString(),tail:b.add(0x18).readPointer().toString()});
 }
 return rows;
};
rpc.exports={finish(){recording=false;return {installed,counts,searchDepth};}};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('CancelQueue208 PE mismatch');
 installed=true;const hook=(r,cb)=>Interceptor.attach(base.add(r),cb);
 hook(0x231df0,{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();if(!value||!value.startsWith(config.prefix))return;
  active=true;const match=value.match(/tick=(\d+)/);if(match)tick=+match[1];
  emit('marker',{value,searchDepth,buckets:buckets(),actors:actors.filter(a=>[50,52,94,95].includes(a.role)).map(snapshot)});
  if(value.includes(' label=complete'))active=false;
 }});
 hook(0x680320,{onEnter(){
  if(!active)return;const unit=this.context.ecx;if(byUnit.has(unit.toString()))return;
  const m=resolve(pair(unit.add(0x16c))),p=m.add(0xa8).readPointer();
  const a={role:actors.length,unit,moverId:pair(m.add(0x14)),pathId:pair(p.add(0x14))};
  actors.push(a);byUnit.set(unit.toString(),a);emit('actor',{actor:a.role,unit:unit.toString(),mover:m.toString(),path:p.toString()});
 }});
 for(const [kind,rva] of [['fine',0x14a4c0],['adaptive',0x163f50]])hook(rva,{
  onEnter(){this.live=active;if(this.live){searchDepth++;emit('search-begin',{kind,depth:searchDepth});}},
  onLeave(ret){if(this.live){emit('search-end',{kind,depth:searchDepth,result:ret.toUInt32()});searchDepth--;}}
 });
 hook(0x171340,{onEnter(args){this.live=active;this.a=null;if(!this.live)return;
  const m=this.context.ecx;this.a=actors.find(a=>resolve(a.moverId).equals(m));
  if(this.a)emit('stop-begin',{searchDepth,arguments:Array.from({length:5},(_,i)=>args[i].toUInt32()),...snapshot(this.a)});
 },onLeave(){if(this.live&&this.a)emit('stop-end',{searchDepth,...snapshot(this.a)});}});
 hook(0x168310,{onEnter(args){this.live=active;this.path=args[0].toString();this.offset=this.context.ecx.sub(base.add(0xd53a90)).toUInt32();},onLeave(ret){if(this.live)emit('admission',{path:this.path,offset:this.offset,result:ret.toUInt32(),searchDepth});}});
 emit('installed',{hooks:6});
}});
