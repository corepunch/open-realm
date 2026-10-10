// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work227Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f544140","6f566dd0","6f543c50","6f543e50","6f543eb0","6f5441e0","6f48e4e0","6f48b850","6f48ecd0","6f69c5c0","6f69c5e0","6f688d90","6f6785c0","6f66fc50","6f693d50"}){
   var fn=getFunctionAt(toAddr(a));if(fn==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(fn.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(fn.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');
    for(byte b:i.getBytes())out.append(String.format("%02x",b&255));
    out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(fn.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  Files.writeString(p,out);println("Saved "+p);
 }
}
