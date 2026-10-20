// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work226Evidence extends GhidraScript {
 public void run() throws Exception {
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f409630","6f436e10","6f48ef40","6f48bca0","6f66fc50","6f693d50","6f688d90","6f6785c0"}){
   var fn=getFunctionAt(toAddr(a)); if(fn==null)throw new Exception(a);
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
