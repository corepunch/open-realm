// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work238Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f6b8c10","NetOrder_AttachPointCandidates","Payoff238: non-FLOAT Alt primary and current-flight special requests retain global candidate order. Complete original attachment/readiness/publication across10 class/grounding configurations proves physical birth after each request's last ready member, not request allocation order. Engine retains separate common points/history contexts, prepares both classes before callbacks and publishes newest-first ranks in constant time at readiness. Mixed FLOAT, dynamic admission category changes and full queued Alt trajectories remain open."},
   {"6f16bcf0","MoveRequest_PublishReadyCohorts","Payoff238: native89cd10 readiness publishes each Alt class independently. Four candidate class orders with forced-ground controls show last-ready ordering, including special request before primary for ground/fly/fly/ground. All candidate attachments precede any readiness. Complete original factory and callback bodies retain native physical flags1000e and row order. Engine stages class membership in stable owners and publishes physical visit rank at each class readiness boundary."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f6b8c10","6f89cd10","6f16d850","6f249b60","6f16bcf0","6f16bdb0","6f16b7b0","6f89caf0"}) {
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
