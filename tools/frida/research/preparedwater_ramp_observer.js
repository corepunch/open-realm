// Read-only prepared-water producer inputs. Bounded diagnostic, no journey claim.
// Observe natural map initialization; never invoke native game functions.
let installed=false,recording=true,building=false,base,seq=0;const counts={};
const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
function emit(event,data={}){counts[event]=(counts[event]||0)+1;send({event,seq:seq++,...data});}
Process.attachModuleObserver({onAdded(module){
 if(installed||module.name.toLowerCase()!=='game.dll')return;base=module.base;
 const pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:module.path});
 Interceptor.attach(base.add(0x748ab0),{onEnter(){
  this.take=recording;this.terrain=this.context.ecx;if(!this.take)return;building=true;
  const t=this.terrain,w=t.add(0xb4).readU32(),h=t.add(0xb8).readU32(),v=t.add(0xe4).readPointer();
  if(w!==16||h!==16||v.isNull())throw Error('unexpected prepared terrain');
  emit('water-build-enter',{width:w,height:h,waterTypes:t.add(0x30c).readU32(),vertices:Array.from({length:289},(_,i)=>words(v.add(i*0x1c),4))});
 },onLeave(){if(!this.take)return;building=false;const t=this.terrain,n=t.add(0x780).readU32();if(n!==256)throw Error('unexpected grid count');emit('water-build-leave',{values:words(t.add(0x784).readPointer(),n)});}});
 Interceptor.attach(base.add(0x7451f0),{onEnter(a){this.take=recording&&building;if(this.take){this.out=a[0];this.x=a[1].toInt32();this.y=a[2].toInt32();}},onLeave(){if(this.take)emit('ground-corner',{x:this.x,y:this.y,value:this.out.readU32()});}});
 Interceptor.attach(base.add(0x7446e0),{onEnter(a){this.take=recording&&building;if(this.take){this.out=a[0];this.x=a[1].toInt32();this.y=a[2].toInt32();}},onLeave(){if(this.take)emit('ramp-offset',{x:this.x,y:this.y,value:this.out.readU32()});}});
 Interceptor.attach(base.add(0x231df0),{onEnter(a){if(!recording||a[0].isNull())return;const value=a[0].readCString();if(value.startsWith(config.prefix))emit('marker',{value});}});
}});
rpc.exports={finish(){recording=false;return{installed,readOnly:true,counts,seq};}};
