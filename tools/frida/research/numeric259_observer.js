// Read-only original source lexer/parser observer; no native calls or writes.
let installed=false,recording=true,tokens=[],results=[],seen=new Set(),windows=new Set();
function install(m){
 if(installed||m.name.toLowerCase()!=='game.dll')return;
 const base=m.base,pe=base.add(base.add(0x3c).readU32());
 if(Process.pointerSize!==4||pe.add(8).readU32()!==config.timestamp||pe.add(80).readU32()!==config.imageSize)throw Error('PE differs');
 installed=true;send({event:'module',base:base.toString(),path:m.path});
 Interceptor.attach(base.add(0x9249d0),{onEnter(){this.lexer=this.context.ecx;},onLeave(value){
  if(!recording)return;const l=this.lexer,key=l.toString(),n=l.add(0xc4).readU32();
  if(n>32768)throw Error('token size');const text=String.fromCharCode(...new Uint8Array(l.add(0x98).readPointer().readByteArray(n))).split('\0')[0];
  if(text==='N259Boundary'){seen.add(key);windows.add(key);}
  if(!windows.has(key))return;
  const token=value.toUInt32(),row={event:'source-token',token,text};
  if(token===0x108||token===0x109)row.word=l.add(0x24).readU32();
  tokens.push(row);send(row);if(text==='endfunction')windows.delete(key);
 }});
 Interceptor.attach(base.add(0x922a50),{onEnter(args){this.lexer=args[0];},onLeave(value){
  if(!recording||!seen.has(this.lexer.toString()))return;
  const row={event:'compile-result',result:value.toUInt32(),error:this.lexer.add(0x8c).readU32()};results.push(row);send(row);
 }});
 Interceptor.attach(base.add(0x925500),{onEnter(args){
  if(!recording||!seen.has(args[0].toString()))return;
  send({event:'compile-error',code:args[1].toUInt32(),line:args[0].add(0xb8).readU32(),context:args[0].add(4).readU32()});
 }});
 Interceptor.attach(base.add(0x231df0),{onEnter(args){if(!recording||args[0].isNull())return;const value=args[0].readCString();if(value.startsWith(config.prefix))send({event:'marker',value});}});
}
Process.attachModuleObserver({onAdded:install});
rpc.exports={finish(){recording=false;return {installed,readOnly:true,tokens:tokens.length,results:results.length,open:windows.size};}};
