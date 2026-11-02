// Read-only public timed-facing request and physical-owner observations.
let installed=false,active=false,recording=true,tick=0,base=null,api=null;
const counts={},watched=new Set();
const emit=(event,data={})=>{if(recording){counts[event]=(counts[event]||0)+1;send({event,tick,...data});}};
const pair=p=>[p.readU32(),p.add(4).readU32()];
const mover=m=>({mover:m.toString(),position:pair(m.add(0x78)),velocity:pair(m.add(0x80)),
 facing:m.add(0x8c).readU32(),turn:m.add(0xb8).readU32(),flags:m.add(0xd8).readU32(),visual:pair(m.add(0xc8)),time:m.add(0x70).readU32(),epoch:m.add(0x74).readU32()});
const members=g=>{
 const n=g.add(0x38).readU32(),data=g.add(0x28).readPointer(),rows=[];
 for(let i=0;i<n&&i<16;i++) {
  const r=data.add(i*0x2c),m=r.add(0x14).readPointer();
  if(!m.isNull())rows.push({identity:pair(r),destination:pair(r.add(0x18)),
   speed:r.add(0x20).readU32(),heading:r.add(0x24).readU32(),memberFlags:r.add(0x28).readU32(),...mover(m)});
 }
 return rows;
};
const path=p=>p.isNull()?null:({destination:pair(p.add(0x1c)),count:p.add(0x70).readU32(),index:p.add(0x78).readU32(),flags:p.add(0x88).readU32()});
const group=g=>({counter:base.add(0xd53a48).readPointer().add(0x538).readU32(),path:path(g.add(0x3c).readPointer()),group:g.toString(),identity:pair(g.add(0x14)),flags:g.add(0x80).readU32(),
 parameter:g.add(0x74).readU32(),age:g.add(0x5c).readU32(),point:pair(g.add(0x54)),members:members(g)});
rpc.exports={finish(){recording=false;return {installed,counts};}};
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 base=module.base;const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw new Error('Facing207 PE mismatch');
 installed=true;const hook=(r,cb)=>Interceptor.attach(base.add(r),cb);
 hook(0x231df0,{onEnter(args){
  if(args[0].isNull())return;const value=args[0].readCString();
  if(!value||!value.startsWith(config.prefix))return;
  active=true;const m=value.match(/tick=(\d+)/);if(m)tick=+m[1];emit('marker',{value});
  if(value.includes(' label=complete'))active=false;
 }});
 hook(0x2151b0,{onEnter(args){
  this.live=active;if(!this.live)return;
  this.previous=api;api={handle:args[0].toUInt32(),angle:args[1].readU32(),duration:args[2].readU32()};
  emit('api-begin',api);
 },onLeave(){if(this.live){if(api.mover){api.snapshot=mover(ptr(api.mover));api.setter=ptr(api.mover).readPointer().add(0x4c).readPointer().sub(base).toString();}emit('api-end',api);api=this.previous;}}});
 hook(0x1eef90,{onEnter(){this.live=api&&this.context.ecx.toUInt32()===api.handle;},onLeave(ret){
  if(this.live&&!ret.isNull())api.unit=ret.toString();
 }});
 hook(0x054530,{onEnter(){this.live=active;},onLeave(ret){
  if(this.live&&!ret.isNull()){if(api)api.mover=ret.toString();watched.add(ret.toString());}
 }});
 hook(0x05c0e0,{onEnter(args){if(active)emit('turn-request',{angle:args[0].readU32(),turn:args[1].readU32()});}});
 hook(0x16b7b0,{onEnter(args){this.req=active?this.context.ecx:null;this.g=args[0];},onLeave(){
  if(this.req)emit('cohort',{requestFlags:this.req.add(0x100).readU32(),requestPoint:pair(this.req.add(0xe8)),...group(this.g)});
 }});
 hook(0x16c150,{onEnter(){
  this.g=active?this.context.ecx:null;this.live=this.g&&members(this.g).some(r=>watched.has(r.mover));
  if(this.live)emit('owner-begin',group(this.g));
 },onLeave(){if(this.live)emit('owner-end',group(this.g));}});
 hook(0x160060,{onEnter(args){
  this.m=this.context.ecx;this.live=active&&watched.has(this.m.toString());
  if(this.live)emit('commit-begin',mover(this.m));
 },onLeave(){if(this.live)emit('commit-end',mover(this.m));}});
 hook(0x05aa80,{onEnter(args){this.out=api?args[0]:null;},onLeave(){if(this.out)emit('getter-out',{out:this.out.readU32()});}});
 hook(0x1705c0,{onEnter(){this.m=this.context.ecx;this.live=active&&watched.has(this.m.toString());if(this.live)emit('visual-begin',mover(this.m));},onLeave(){if(this.live)emit('visual-end',mover(this.m));}});
 emit('installed',{hooks:11});
}});
