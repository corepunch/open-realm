// Read-only stable function entries. No calls into game code and no writes.
let installed=false,active=false,complete=false,finished=false,seq=0,tick=0,scene=0;
const counts={};let lastUnit=null;const queries=[];
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('image mismatch');
 installed=true;const at=r=>base.add(r),w=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 const emit=(event,data={})=>{if(finished||(!active&&event!=='module'))return;counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
 const unit=u=>u.isNull()?null:{pointer:u.toString(),identity:w(u.add(12)),rawcode:u.add(0x30).readU32(),flags:u.add(0x5c).readU32(),suspension:u.add(0x54).readU32(),invisibility:u.add(0x114).readU32(),invisibilityScalar:w(u.add(0x118),6),availability:w(u.add(0x1b8),8),clock_depth:u.add(0x1d4).readU32(),attack_flags:u.add(0x1e8).readPointer().isNull()?null:u.add(0x1e8).readPointer().add(0x20).readU32()};
 const buff=b=>({pointer:b.toString(),identity:w(b.add(12)),vtable:b.readPointer().sub(base).toUInt32(),flags:b.add(0x20).readU32(),depth:w(b.add(0x3c),4),unit:unit(b.add(0x30).readPointer()),timer:w(b.add(0x84),7),elapsed:b.add(0xa4).readU32(),remaining:b.add(0xac).readU32()});
 const hook=(r,c)=>Interceptor.attach(at(r),c);
 hook(0x204100,{onEnter(){this.q={};queries.push(this.q);},onLeave(){queries.pop();if(this.q.u)lastUnit=this.q.u;}});
 hook(0x1eef90,{onLeave(r){if(queries.length&&!r.isNull())queries[queries.length-1].u=ptr(r.toString());}});
 hook(0x231df0,{onEnter(a){if(a[0].isNull())return;const v=a[0].readCString();if(!v.startsWith(config.prefix))return;
  if(v.includes(' label=start'))active=true;
  const t=v.match(/tick=(\d+)/),s=v.match(/scene=(\d+)/);if(t)tick=+t[1];if(s)scene=+s[1];
  emit('marker',{value:v});if(v.includes('label=pose ')&&lastUnit){emit('unit-state',{unit:unit(lastUnit)});lastUnit=null;}
  if(v.includes(' label=complete')){complete=true;active=false;}
 }});
 for(const r of [0x427610,0x43b0e0,0x531420,0x532fa0,0x531a80,0x532b50,0x63ce40])hook(r,{onEnter(a){this.live=active&&[0xb3fca8,0xb40014,0xb0aa60,0xb0a6e4].some(v=>this.context.ecx.readPointer().equals(at(v)));if(this.live){this.b=ptr(this.context.ecx.toString());emit('buff-enter',{rva:r,buff:buff(this.b),argument:a[0].toUInt32(),duration:(r===0x532b50?a[0]:r===0x63ce40?a[1]:null)?.readU32()??null,fade:r===0x63ce40?a[2].readU32():null});}},onLeave(){if(this.live)emit('buff-leave',{rva:r,buff:buff(this.b)});}});
 for(const r of [0x6696e0,0x694b20,0x48e460,0x68b780])hook(r,{onEnter(a){this.live=active;if(this.live){this.u=ptr(this.context.ecx.toString());emit('unit-enter',{rva:r,unit:unit(this.u),argument:a[0].toUInt32(),fade:(r===0x6696e0||r===0x694b20)?a[0].readU32():null});}},onLeave(){if(this.live)emit('unit-leave',{rva:r,unit:unit(this.u)});}});
 hook(0x59daa0,{onEnter(a){if(active)emit('windwalk-setup',{unit:unit(a[0]),duration:a[1].readU32(),fade:a[2].readU32(),speed:a[3].readU32(),damage:a[4].readU32(),alias:a[7].toUInt32()});}});
 // Ghost uses the ability prefix only; never read timed-Binv fields on it.
 const passive=b=>({pointer:b.toString(),identity:w(b.add(12)),vtable:b.readPointer().sub(base).toUInt32(),flags:b.add(0x20).readU32(),depth:w(b.add(0x3c),4),unit:unit(b.add(0x30).readPointer())});
 for(const r of [0x6311f0,0x62e400,0x643f60])hook(r,{onEnter(){this.live=active&&this.context.ecx.readPointer().equals(at(0xb67778));if(this.live){this.b=ptr(this.context.ecx.toString());emit('ghost-enter',{rva:r,ability:passive(this.b)});}},onLeave(){if(this.live)emit('ghost-leave',{rva:r,ability:passive(this.b)});}});
 // Stable recovery entry: forced is stack+4; incoming capture word is stack+8 (ret8).
 hook(0x49d280,{onEnter(a){if(active)emit('recovery-enter',{return_rva:this.returnAddress.sub(base).toUInt32(),forced:a[0].toUInt32(),capture:a[1].toUInt32(),ability:passive(this.context.ecx)});}});
 emit('module',{base:base.toString()});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){finished=true;active=false;return {installed,complete,counts,sequence:seq,dropped:0,overflow:false,readOnly:true,pending_queries:queries.length};}};
