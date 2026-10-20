// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work236Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f5faaf0","Move_TryPreviousRequestCohort","Payoff236: unchanged complete callback/native factories prove source attaches pending before old resolved peers. Each peer ready89cd10 attempts virtual publication but source pending makes169c50 conflict and16bcf0 return-1. Old physical ownership remains until source ready. Fresh publication reruns ordered16b7b0 partition; old membership is not permission to skip distance checks. Engine queued activation now shares ready partition with direct admission;48 native cases include all preferred-policy masks, old velocity and transitive/fractional boundaries. Supplied callback context/cached world and Storm storage are explicit; public observer separately witnesses pending/source-ready order."},
   {"6f169620","MoveRequest_RetainCandidate","Payoff236: canonical candidate identity and readiness are separate. readyBoolean0 attaches identity but storesffffffff in readyac. This is pending, not an absent candidate. Complete5faaf0 peer-ready publication waits for source."},
   {"6f16bcf0","MoveRequest_PublishReadyCohorts","Payoff236: peer-ready attempts inside queued callback see source pending and return-1 with balanced fine member exclusions. Once source readiness is published, rerun the ordinary ordered distance partition on all ready rows, including peers formerly in one physical group."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f5faaf0","6f89cd10","6f249b60","6f169620","6f16d850","6f16bcf0","6f169c50","6f169d60","6f16b7b0","6f16d1c0","6f16dd70"}) {
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
