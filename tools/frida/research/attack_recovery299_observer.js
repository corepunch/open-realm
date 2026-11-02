// Original1.27 read-only F19 observer. Stable entries, no game calls or writes.
let installed=false,active=false,tick=0,scene=0,seq=0,completed=false,finished=false;
const counts={},roles=new Map(),queries=[];let lastX=null;
// Exact E8 sites from retail_attack_recovery298_static.json; callers below remain return RVAs.
const recoveryCallSites=new Set([0x497e06,0x49cec6,0x49cf0c,0x49e6c2,0x49a3ff,0x49935a,0x49b58f,0x49b240,0x4953a4,0x495420,0x494313,0x497ba8,0x4980f7,0x58cd36,0x5e80f8,0x5fc245,0x628a5d]);
const words=(p,n=2)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
function install(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;
 const base=module.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('wrong original image');
 installed=true;
 const at=r=>base.add(r),rva=p=>p.sub(base).toUInt32();
 const clock=()=>{const o=at(0xd53a48).readPointer();return o.isNull()?null:{counter:o.add(0x538).readU32(),primary:words(o.add(0x54),4)};};
 const emit=(event,data={})=>{if(finished||(!active&&event!=='module'))return;counts[event]=(counts[event]||0)+1;send({event,seq:++seq,tick,scene,...data});};
 const hook=(r,c)=>Interceptor.attach(at(r),c);
 const unit=u=>{if(u.isNull())return null;const identity=words(u.add(12));return {pointer:u.toString(),identity,role:roles.get(identity.join(':'))??0,rawcode:u.add(0x30).readU32(),owner:u.add(0x58).readU32(),widget:u.add(0x20).readU32(),flags:u.add(0x5c).readU32(),flags60:u.add(0x60).readU32(),head:words(u.add(0x19c)),task:words(u.add(0x174))};};
 const attack=a=>({pointer:a.toString(),identity:words(a.add(12)),unit:unit(a.add(0x30).readPointer()),flags:a.add(0x20).readU32(),current:words(a.add(0x6c)),alternative:words(a.add(0x78)),suppression:a.add(0x220).readU32()});
 const buff=b=>({pointer:b.toString(),identity:words(b.add(12)),flags:b.add(0x20).readU32(),unit:unit(b.add(0x30).readPointer())});
 const state=m=>({mover:m.toString(),pose:words(m.add(0x70),8),heading:m.add(0x8c).readU32(),radius:m.add(0x90).readU32(),range:m.add(0xb0).readU32(),motion_parameters:words(m.add(0xb4),3),flags:m.add(0xd8).readU32(),group:words(m.add(0x9c)),request:words(m.add(0xc0))});
 // A public pose row binds a role through the real GetUnitX handle resolver.
 hook(0x204100,{onEnter(){this.q={resolutions:[]};queries.push(this.q);},onLeave(ret){queries.pop();if(this.q.unit)lastX={unit:this.q.unit,raw:ret.toUInt32(),resolutions:this.q.resolutions};}});
 // InvocationReturnValue is reused by Frida: clone it before crossing callbacks.
 hook(0x1eef90,{onLeave(ret){if(queries.length&&!ret.isNull())queries[queries.length-1].unit=ptr(ret.toString());}});
 hook(0x231df0,{onEnter(a){if(a[0].isNull())return;const value=a[0].readCString();if(!value.startsWith(config.prefix))return;
  if(value.includes(' label=start'))active=true;
  const t=value.match(/tick=(\d+)/),s=value.match(/scene=(\d+)/);if(t)tick=+t[1];if(s)scene=+s[1];
  if(value.includes('label=pose ')){const r=value.match(/role=(\d+)/);if(!r||!lastX)throw Error('missing public unit role resolver');roles.set(words(lastX.unit.add(12)).join(':'),+r[1]);emit('role-binding',{role:+r[1],unit:unit(lastX.unit),x:lastX.raw,resolutions:lastX.resolutions,clock:clock()});lastX=null;}
  emit('marker',{value,clock:clock()});if(value.includes(' label=complete')){completed=true;active=false;}
 }});
 for(const [r,name]of [[0x49d280,'recover'],[0x49b420,'target-lost'],[0x49b160,'target-owner'],[0x497ba0,'attack-disable'],[0x497e20,'release-target'],[0x495740,'suppression']])
  hook(r,{onEnter(a){this.live=active;if(!this.live)return;this.a=this.context.ecx;emit(name+'-enter',{attack:attack(this.a),caller:rva(this.returnAddress),...(r===0x49d280?{forced:a[0].toUInt32(),capture_word:a[1].toUInt32(),call_instruction:recoveryCallSites.has(rva(this.returnAddress)-5)?rva(this.returnAddress)-5:null}:{}),clock:clock()});},onLeave(){if(this.live)emit(name+'-leave',{attack:attack(this.a),clock:clock()});}});
 hook(0x497190,{onEnter(){this.live=active;if(this.live){this.a=this.context.ecx;emit('task-pop-enter',{attack:attack(this.a),clock:clock()});}},onLeave(ret){if(this.live)emit('task-pop-leave',{attack:attack(this.a),result:ret.toUInt32()&255,clock:clock()});}});
 hook(0x692120,{onEnter(a){if(active)emit('point-task',{unit:unit(this.context.ecx),caller:rva(this.returnAddress),code:a[0].toUInt32(),point:[a[1].readU32(),a[2].readU32()],range:a[3].readU32(),association:a[4].toString(),clock:clock()});}});
 hook(0x6926b0,{onEnter(a){if(active)emit('target-task',{unit:unit(this.context.ecx),caller:rva(this.returnAddress),code:a[0].toUInt32(),target:a[1].toString(),range:a[2].readU32(),clock:clock()});}});
 const bridgeCalls=[];
 hook(0x05b970,{onEnter(a){this.live=active;if(!this.live)return;this.row={bridge:this.context.ecx.toString(),identity:words(this.context.ecx.add(8)),point:[a[0].readU32(),a[1].readU32()],arrival:a[2].toUInt32(),cantpath:a[3].toUInt32(),receiver:a[4].toString(),speed:a[5].readU32(),flag:a[6].toUInt32(),range:a[7].readU32(),clock:clock()};bridgeCalls.push(this.row);emit('point-request',this.row);},onLeave(){if(this.live)bridgeCalls.pop();}});
 hook(0x05a5c0,{onEnter(a){this.live=active;if(!this.live)return;this.row={bridge:this.context.ecx.toString(),identity:words(this.context.ecx.add(8)),target_identity:words(a[0].add(8)),range:a[1].readU32(),persistent:a[5].toUInt32(),clock:clock()};bridgeCalls.push(this.row);emit('target-request',this.row);},onLeave(){if(this.live)bridgeCalls.pop();}});
 // Resolve payload identities during public GetUnitX as well as request setup.
 // 054530 is a generic registry resolver. Keep payloads opaque here; associate them
 // with actual mover-hook pointers offline, never assume every payload is a mover.
 // A bridge identity is not a Unit canonical identity; never equate the two.
 hook(0x054530,{onEnter(){this.query=queries.length?queries[queries.length-1]:null;this.live=active&&bridgeCalls.length>0;this.identity=[this.context.ecx.toUInt32(),this.context.edx.toUInt32()];if(this.live)this.row={identity:this.identity,request:bridgeCalls[bridgeCalls.length-1]};},onLeave(ret){if(this.query&&!ret.isNull())this.query.resolutions.push({identity:this.identity,payload:ret.toString()});if(this.live)emit('bridge-resolve',{...this.row,payload:ret.toString(),clock:clock()});}});
 // Exact279 ABI; keep every global arrival/velocity record in declared interval.
 hook(0x16e910,{onEnter(a){this.live=active;if(!this.live)return;this.m=this.context.ecx;this.angle=a[4];this.inside=a[5];this.row={source:words(a[0]),heading:a[1].readU32(),threshold:a[2].readU32(),destination:words(a[3]),before:state(this.m),clock:clock()};},onLeave(ret){if(this.live)emit('arrival',{...this.row,result:ret.toUInt32(),angle:this.angle.readU32(),inside:this.inside.readU32()});}});
 hook(0x16fe20,{onEnter(a){this.live=active;if(!this.live)return;this.m=this.context.ecx;this.row={speed:a[0].readU32(),heading:a[1].readU32(),before:state(this.m),clock:clock()};},onLeave(){if(this.live)emit('velocity',{...this.row,after:state(this.m)});}});
 hook(0x679db0,{onEnter(){if(active)emit('invisibility-notify',{unit:unit(this.context.ecx),caller:rva(this.returnAddress),clock:clock()});}});
 hook(0x642dd0,{onEnter(a){if(active)emit('invisibility-buff-event',{buff:buff(this.context.ecx),event:words(a[0],4),clock:clock()});}});
 hook(0x628a10,{onEnter(){if(active)emit('invisibility-recover',{buff:buff(this.context.ecx),caller:rva(this.returnAddress),clock:clock()});}});
 hook(0x63ce40,{onEnter(a){if(active)emit('invisibility-setup',{buff:buff(this.context.ecx),recipient:unit(a[0]),context:a[1].toString(),fade:a[2].isNull()?null:a[2].readU32(),clock:clock()});}});
 emit('module',{base:base.toString()});
}
Process.attachModuleObserver({onAdded:install});
// No row cap or sampling: every callback in the declared interval is emitted.
rpc.exports={finish(){finished=true;active=false;return {installed,counts,readOnly:true,complete:completed,sequence:seq,dropped:0,overflow:false,pending_public_queries:queries.length};}};
