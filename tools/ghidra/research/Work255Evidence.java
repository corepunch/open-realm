// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work255Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f053630","SimClock_RearmPeriodicTimer","Payoff255: guard d01ad uses this event-timer rearm, not agent053710. Advance request4 from popped clock40 plus period8, preserving request identity and unsigned serial14. No new serial is allocated for an inside guard poll; stable tie ordering and catch-up survive engine cold save."},
 {"6f0542d0","SimClock_DispatchTimerRequest","Payoff255: guard requests dispatch receiver virtualc here, unlike agent054370 virtual48. Callback cancellation prevents periodic rearm; otherwise053630 reinserts the same request and serial. Read-only guard-execute/rearm capture hooks identify this actual caller class."},
 {"6f060ca0","EventClock_DispatchReceiver","Payoff255: publishes request1c as packet code and calls retained receiver virtualc. Guard timer control is the request receiver at18, while Attack is control8. Track the guard control from0608d0; filtering the unrelated agent dispatcher054370 misses guard requests."},
 {"6f499030","Attack_OnGuardPoll","Payoff255: inside range returns directly, leaving the original periodic request to0542d0/053630. Outside tail-calls497330 which cancels/replaces it. Engine must not restart its guard timer on every inside poll: that changes serial/tie ownership."},
 {"6f49d220","Attack_StartGuardEvaluation","Payoff255: periodic d01ad initial arm allocates a request, but each subsequent inside poll retains that request serial via event timer dispatcher0542d0 and rearm053630. Explicit guard-task reevaluation may cancel and allocate anew. Distinguish these producers."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  var dm=currentProgram.getDataTypeManager();
  var t=new StructureDataType(new CategoryPath("/WC3/Pathfinding"),"WC3AttackGuardPrefix",0x2b8);
  t.replaceAtOffset(0,PointerDataType.dataType,4,"vtable",null);
  t.replaceAtOffset(0x20,UnsignedIntegerDataType.dataType,4,"flags","400 returning;2000000 guard task evaluated.");
  t.replaceAtOffset(0x30,PointerDataType.dataType,4,"unit",null);
  t.replaceAtOffset(0x27c,FloatDataType.dataType,4,"guard_range","Authored Misc.GuardDistance captured by weapon binding.");
  t.replaceAtOffset(0x2ac,FloatDataType.dataType,4,"guard_x",null);
  t.replaceAtOffset(0x2b4,FloatDataType.dataType,4,"guard_y",null);
  dm.addDataType(t,DataTypeConflictHandler.DEFAULT_HANDLER);
  StringBuilder out=new StringBuilder();
  out.append("TYPE ").append(dm.getDataType("/WC3/Pathfinding/WC3AttackGuardPrefix")).append("\n");
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append("\n");
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
