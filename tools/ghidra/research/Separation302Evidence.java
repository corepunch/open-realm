// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Separation302Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f673610","Unit_CancelAllOrders","Payoff302 SEP-01.2: Invoke67e0e0 only when Unit5c physical100000 is set. Scripted pause recorded while world-absent has no suspended task contribution to cancel; removal must not invent its inverse or clear the unconstructed public pause."},
   {"6f694690","Unit_QueueRemovalTasks","Payoff302 SEP-01.2: Unit_CancelAllOrders precedes688d90. Replace old suspension ownership before acquiring the removal contribution; depth1 remains across pending owner/pause refresh."},
   {"6f69c510","Unit_CommitRemovalInactivity","Payoff302 SEP-01.2: After death/pathing finalization and69d1c0(1), release removal Unit198 contribution. Unchanged204 read-only repeats observe depth0 with a fresh repulsor before mover destruction."},
   {"6f67e0e0","Unit_CancelSuspendedTask","Payoff302 SEP-01.2: Cancellation clears scripted/suspend flags and decrements suspension depth before6785c0. Unchanged204 paused-removal witness recreates separation at depth0 before the new removal acquire."},
   {"6f688d90","Unit_AcquireSeparationSuppression","Payoff302 SEP-01.2: Signed32 INC then693d50 refresh. Removal owns one independently saved contribution, separate from Pause and construction. No saturation or deferred-lifetime predicate substitutes for depth."},
   {"6f6785c0","Unit_ReleaseSeparationSuppression","Payoff302 SEP-01.2: Signed32 DEC then693d50 refresh. Final removal inverse runs while unit remains queryable and may recreate separation. Physical mover destruction subsequently removes that new list head."},
   {"6f693d50","Unit_RefreshSeparationConfiguration","Payoff302 SEP-01.2: Every removal acquire/inverse reads current authored selector, owner/category and rank. Nested owner1 gives11110000 at final reconfiguration; owner0 gives10110000 in unchanged204 repeats."},
   {"6f66fc50","Unit_CanUseSeparation","Payoff302 SEP-01.2: Complete predicate has no dead100 or pending-removal-identity test. Positive signed198, channel/suspend/script-pause gates are authoritative. Thus final removal inverse can legitimately recreate a repulsor on an inactive unit."},
   {"6f1710e0","Mover_ConfigureSeparation","Payoff302 SEP-01.2: Each refresh destroys the old object; enabled configuration allocates and prepends. Final removal inverse is a real transient head insertion followed by mover destruction, not a retained old object."}
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
