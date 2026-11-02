// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work240Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f6b8c10","NetOrder_AttachPointCandidates","Payoff240 GROUP-04.6: FLOAT Unit1fc10 always attaches the second request; non-FLOAT Alt effective flyers attach the third, others primary.48 complete native attachment/readiness/publication domains cover float masks0/2/5/15, flight masks0/6/15, Alt0/1 and forced-ground0/1. Two ordinary and two Alt actual four-type selected repeats confirm FLOAT/primary/special affinity. Engine prepares up to three populated classes in bounded linear time, retains common clicked points and distinct queue/history contexts. Synthetic float-plus-flight combinations certify attachment, not6ba800's later association priority."},
   {"6f16bcf0","MoveRequest_PublishReadyCohorts","Payoff240: physical birth follows each canonical class's final readiness. The complete native kernel supplies candidate-index readiness order; it does not certify UI admission sorting. Actual mixed FLOAT Alt public capture binds primary then FLOAT then special after9-word-row sorting, despite attachment order primary/FLOAT/special/primary. Engine class separation is integrated; full6bcc40 admission ordering/callback chronology remainsGROUP-04.6."},
   {"6f6bcc40","NetOrder_ComparePointCandidates","Payoff240: genuine ordinary/Alt mixed FLOAT/fly selection repeats retain distinct physical publication order after this nine-word comparator. Do not equate attachment iteration with sorted order admission. New native readiness matrix supplies its order explicitly; no broad public row-priority parity is claimed by that matrix."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f6b8c10","6f89cd10","6f16d850","6f249b60","6f16bcf0","6f16bdb0","6f16b7b0","6f89caf0","6f6b9f70","6f6bcc40"}) {
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
