// Read-only group-route publication and nested coarse request evidence.
let installed=false,recording=true,phase=-1,count=0,active=[];
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 const words=(p,n)=>Array.from({length:n},(_,i)=>p.add(i*4).readU32());
 function state(path){const n=path.add(0x70).readU32();if(n>4096)throw Error('table count differs');return{flags:path.add(0x88).readU32(),goal:words(path.add(0x24),2),count:n,index:path.add(0x78).readU32(),capacity:path.add(0x6c).readU32(),points:n?words(path.add(0x60).readPointer(),n*2):[],timestamps:words(path.add(0x7c),2)};}
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(args[0].isNull())return;const value=args[0].readCString();if(recording&&value.startsWith(config.prefix)){const match=/ begin=(\d+)/.exec(value);if(match)phase=Number(match[1]);send({event:'marker',value});}}});
 Interceptor.attach(base.add(0x166c30),{onEnter(){if(active.length&&this.context.ecx.equals(active[active.length-1].path))active[active.length-1].searches++;}});
 Interceptor.attach(base.add(0x167120),{onEnter(args){
  this.take=recording&&phase>=0&&count<1024;if(!this.take)return;count++;
  this.path=this.context.ecx;this.searches=0;this.row={event:'route',phase,straight:args[1].toUInt32(),before:state(this.path)};active.push(this);
 },onLeave(ret){if(!this.take)return;if(active.pop()!==this)throw Error('unbalanced route');this.row.after=state(this.path);this.row.searches=this.searches;this.row.result=ret.toUInt32();send(this.row);}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,count,active:active.length};}};
