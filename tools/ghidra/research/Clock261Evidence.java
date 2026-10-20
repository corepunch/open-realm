// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Clock261Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f054190","SimClock_AdvanceRequests","Payoff261 SCHED-01.2: original composed80 advances from supplied299.875 across300; old-span callbacks precede0521f0 rebase and remainder callbacks. Pending repeating.125 request remains ordered; presentation clock unchanged. Starting time and six-step owner visits are controlled inputs. Public natural movement wrap and backward UI load are independently covered by unchanged117/181 captures."},
 {"6f0521f0","SimClock_RebaseRequestDeadlines","Payoff261: rebase adjusts queued raw deadlines and request-clock epoch only. It does not touch PathOwner+538 visits or any path7c/80 timestamp, work budget, FIFO identity/link or countdown. Composed original advance/scheduler oracle and production saved-suffix regression retain these independent domains."},
 {"6f15cea0","SimClock_ElapsedBetween","Payoff261: thiscall ECX clock, five stack args out/current-scalar/current-epoch/old-scalar/old-epoch, RET14. Software subtract then strict abs(delta)<38d1b717 clears fractional delta before signed epoch-span addition.80 exact original results compare through production Move pose commits across a wrap; no host-millisecond replacement."},
 {"6f167310","PathScheduler_UpdateAll","Payoff261: thirteen supplied owner visits across primary-clock rollover retain all four budget countdowns and request FIFO orders. This updater has no clock-time input; only owner visitation drives clearing/reload. Changing clocks must not restart these pools."},
 {"6f167fa0","PathScheduler_UpdateBucket","Payoff261: zero countdown clears work/reloads; nonzero decrements. Primary clock epoch wrapping is not a clearing condition. All four policies match through saved engine restoration with pending fine/coarse requests."},
 {"6f168910","Path_CheckRequestInterval","Payoff261: modes0/1 retain owner-visit timestamps independently of scalar clock epoch. Composed native controls retain all twelve timestamps across rollover. Engine snapshot restores timestamps and FIFO insertion order, then resumes the exact suffix; public backward-time transition is ReadGame, not an invented negative clock increment."},
 {"6f168310","PathScheduler_Admit","Payoff261: complete original admission runs after interval checking for three supplied requesters in order2/0/1 across13 owner visits. Work exhaustion cannot reorder head/tail when primary clock wraps. All four policy outputs and timestamp words match production Move and saved continuation."},
 {"6f15c490","PathOwner_LoadHeaderAndClocks","Payoff261: existing181 UI-load capture proves a public backward primary-clock transition: saved clocks/absolute requests restore verbatim. Unchanged117 public movement save continuations include epoch1 to epoch0 restoration and exact320 committed poses. Do not model load by integrating live movers backward or preserving outgoing request links."}
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append('\n').append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
