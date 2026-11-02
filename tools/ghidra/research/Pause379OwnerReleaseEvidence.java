// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import com.google.gson.*;
import java.nio.file.*;
public class Pause379OwnerReleaseEvidence extends GhidraScript {
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new IllegalStateException("Wrong DLL");
  String[] args=getScriptArgs();if(args.length!=2)throw new IllegalStateException("fresh output and read|apply required");
  Path path=Path.of(args[0]);if(Files.exists(path))throw new IllegalStateException("fresh output required");
  String[][] notes={{"6f48c8c0","Unit_AdjustAbilityDisableContributions","Payoff379 / SEP-01.2: the signed unit contribution changes before the ability walk, which retains the next identity before each callback. The inverse continues across all attached applicable owners while removal is pending. Unchanged removal304 captures provide two observations and one control with 36 matching public markers each. In paused scenes 2 and 3, Inventory, Defend, Attack and Move each change counters 3c/40 from 1/1 to 0/0 before the first 600433 cleanup and before RemoveUnit returns. The engine inverse now preserves this complete walk while retaining physical-liveness, incarnation and linked-record guards. The acquire walk is unchanged. Native and engine verification passes 45 tests / 10,755 assertions per edition, with 506 instruction guards across 12 complete functions."},{"6f40bb00","Ability_RemoveDisableContribution","Payoff379 / SEP-01.2: the inverse subtracts order counter 40 before availability counter 3c, then invokes the concrete resume callback on a positive-to-nonpositive crossing. Pending removal does not replace the complete 48c8c0 traversal. Frozen removal304 per-owner snapshots verify all four attached retail owners before and after public RemoveUnit. The engine fixture checks every applicable owner separately and does not claim the same four-owner inventory."},{"6f694690","Unit_QueueRemovalTasks","Payoff379 / SEP-01.2: Unit_CancelAllOrders runs before the new removal hold on Unit198. Canceling the old d0005 suspension releases scripted availability through 6977f0 and the complete 48c8c0 inverse before cleanup returns. Removal304 paused scenes 2 and 3 retain counters 0/0 for all four attached owners until scheduled 69c510 retirement. Final Move first-disable recovery at 5fc18a observes 3c/40 = 1/0 before the order-counter increment. The original final-stage assertions remain intact; the corrected suite passes 614/614 assertions per edition."}};
  JsonObject out=new JsonObject(),instructions=new JsonObject(),functions=new JsonObject();
  DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try{for(String entry:new String[]{"6f48c8c0","6f40bb00","6f4076e0","6f694690","6f673610","6f6977f0","6f6977a0","6f678590","6f48e460","6f69c510","6f5fc120","6f600340"}) {
   var f=getFunctionContaining(toAddr(entry));if(f==null)throw new IllegalStateException(entry);
   if(args[1].equals("apply"))for(String[] note:notes)if(note[0].equals(entry)) {
    if(f.getName().startsWith("FUN_"))f.setName(note[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
    String prior=f.getComment();if(prior==null)prior="";
    if(!prior.contains(note[2]))f.setComment(prior+"\n"+note[2]);
   }
   JsonObject row=new JsonObject();row.addProperty("entry",f.getEntryPoint().toString());row.addProperty("observed_entry",entry);row.addProperty("name",f.getName());row.addProperty("end",f.getBody().getMaxAddress().toString());
   var result=decomp.decompileFunction(f,45,monitor);if(!result.decompileCompleted())throw new IllegalStateException(result.getErrorMessage());
   row.addProperty("decompiled",result.getDecompiledFunction().getC());
   JsonArray refs=new JsonArray();for(var ref:getReferencesTo(f.getEntryPoint())){
    JsonObject r=new JsonObject();var parent=getFunctionContaining(ref.getFromAddress());
    r.addProperty("site",ref.getFromAddress().toString());r.addProperty("type",ref.getReferenceType().toString());r.addProperty("entry",parent==null?"":parent.getEntryPoint().toString());refs.add(r);
   }row.add("references",refs);
   StringBuilder assembly=new StringBuilder();int count=0;var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var ins=it.next();StringBuilder bytes=new StringBuilder();for(byte b:ins.getBytes())bytes.append(String.format("%02x",b&255));instructions.addProperty(ins.getAddress().toString(),bytes.toString());assembly.append(ins.getAddress()).append(' ').append(ins).append('\n');count++;}
   row.addProperty("assembly",assembly.toString());row.addProperty("instruction_count",count);functions.add(entry,row);

  }}finally{decomp.dispose();}
  out.add("functions",functions);out.add("instructions",instructions);
  Files.writeString(path,new GsonBuilder().setPrettyPrinting().create().toJson(out)+"\n");println(instructions.size()+" guards in "+functions.size()+" complete functions");
 }
}
