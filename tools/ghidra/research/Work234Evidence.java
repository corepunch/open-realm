// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work234Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f167120","Path_RequestGroupRoute","Payoff234: complete36 unchanged-original cases prove adaptive-disabled group requests replace any old table with one scaled adjusted-destination point, index0, capacity128, without interval/admission or7c/80 timestamp writes. Fresh genuine flight-rebind repeats show the same one-point path and zero166c30 calls; engine now retains the group cache and stored destination instead of clearing count on every owner visit."},
   {"6f16ce10","PathGroup_RequestRoute","Payoff234: disabled167120 success still calls1697a0 refresh, publishes group20000 and leaves a valid one-point adaptive cache. Later unchanged destinations bypass replacement; OpenRealm reports rebuilt only for a real replacement, preserving age/counters on cached owner visits and cold save."}
  };
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);
   var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f167120","6f167d70","6f16ce10","6f16e430","6f1485f0","6f14a760"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
