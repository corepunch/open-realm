// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Goal310Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f148850","PathFine_CellContainsTarget","Payoff310: target completion reads the first nonmetadata record with the exact pooled target identity; INSERT hits, REMOVE shadows older insertion. No terrain mask, counted exclusion, dynamic flags or query stamp enters this predicate. Keep this separate from1489a0 blocking-cell evaluation."},
   {"6f14a710","PathFine_TestTargetCell","Payoff310: unsigned cell bounds and live target38 gate the raw identity chain. Outside cells and retired targets are misses; ordinary target object and captured region use the same lookup. Matching current owner coordinates or a reusable edict index is insufficient."},
   {"6f148790","PathFine_TargetOnPerimeter","Payoff310: after166310 floors the fine source, classes0..3 scan clockwise rings with offsets1,2,2,3 and square widths3,4,5,6. Top row left-to-right, right column, bottom row reversed, left column without repeated corners. Early first target hit; interior and terrain admission are irrelevant."},
   {"6f166310","Path_IsSourceOnTargetPerimeter","Payoff310:167290 checks the retained region before retry, falling back to the current target mover when its cached region retires.96 all-class foot/fly/amph goal scenes include persistent Footman and one-shot Farm approaches. For radius8/24/40 Farm approaches, the successful perimeter transition is required to retire the public Move despite identical final physical pose."},
   {"6f167290","Path_CheckTargetOrRetry","Payoff310: complete read-only Farm target scenes expose return3 at owners2643,4443,6242 etc; the following commit completes the task. A perimeter hit returns before retry98, buffer reset or owner RNG. Engine endpoint consumption previously omitted this branch, inventing a failed retry and retaining Move for nine scenes. Preserve ordinary1/4 retry semantics."},
   {"6f16fbd0","Mover_StepRoute","Payoff310: status3 and ready status4 both force the physical range gate after Path_Advance; only4 sets the changed output/member20000. Neither bypasses heading tolerance. The following visit reports arrived and clears physical force. Readiness remains Path_CheckDestinationChange local output; do not force while a changed destination is throttled."},
   {"6f16a790","PathGroup_StepMember","Payoff310: successful target-perimeter3 must not set member20000. The next visit sets10000 when the forced range and independent angular gate accept. Full96-scene before/after scalar sequences and first fine routes cover four footprint classes across foot/fly/amph, outside goals, overlaps, removals and queryzero."},
   {"6f16c390","PathGroup_DispatchCompletion","Payoff310: one-shot Farm approach releases physical association and public Move after successful perimeter arrival; persistent Footman Follow stays active. Radius56 Farm approaches already satisfy their larger ordinary range and provide a negative control for the missing status3 branch. Compare order lifetime as well as final pose."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c==null||!c.contains(row[2]))f.setComment((c==null?"":c+"\n")+row[2]);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
