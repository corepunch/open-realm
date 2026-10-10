// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work235Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f16b7b0","MoveRequest_BindCandidateCohort","Payoff235: complete192 unchanged-original scopes retain native row construction/append, mover binding, ready-slot consumption and depth-first candidate order. Default truncated fine limit40/work60, canonical100 limit90/work150; canonical200 consumes only its seed. Either disabled path uses software sqrt then wrapped integer; both enabled use1627e0 with SOURCE lane, maximum coarse footprint and warp1. Fine prediction includes old velocity. Preferred direct-cell distance differs at fractional boundaries; do not substitute one Euclidean predicate or star clustering. Engine now partitions ready members using bounded12-row DFS and cached adaptive distance."},
   {"6f16bdb0","MoveRequest_ActivateCandidateCohort","Payoff235: physical allocation occurs per surviving outer candidate, after prior recursive connected candidates are consumed. Copy policy/shared/target independently for each physical owner; ready-member partition and saved shared request identity are distinct from unfinished persistent canonical request lifetime."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f16b7b0","6f1691b0","6f16c060","6f16da80","6f170fa0","6f1627e0","6f16bdb0"}) {
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
