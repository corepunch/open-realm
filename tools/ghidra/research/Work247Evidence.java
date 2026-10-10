// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work247Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f05ca50","Payoff247: full original05ca50 ->171340 ->170080 executes through real registry, footprint, placement and publication in32 cases plus32 controls. Bridge exclusion spans velocity cancellation, embedded recovery and path invalidation. Engine S_StopUnitMovementWithRecovery owns this complete bounded sequence before public Stop stand or replacement Move admission. No allocation, world scan or save field."},
   {"6f05bd30","Payoff247: outer Stop release resolves bridge identity/current mover fine again; inner170080 instead retains its acquired record. Do not replace both with one captured pointer. Actual public Stop repeats show bridge counter2, inner footprint3, bridge release1, preserving an independent outer unit scope."},
   {"6f171340","Payoff247: detach and zero velocity precede recovery; path invalidation follows recovery. Callback null disables embedded recovery but retains outer bridge exclusion. Engine completed bridge recovery precedes public stand/queued successor; inner-only183 regression now directly invokes inner recovery, preserving its unchanged expectations."},
   {"6f170080","Payoff247: two read-only public Stop repeats cover clear/admitted/exhausted and pathing-disabled cases;8 bridge scopes each,3 placement searches and1 commit, all nested counters restored. A supplied flat support lookup is the only nonstorage adapter in the32-case complete native kernel;654060 runs unchanged. Notification reentrancy and arbitrary support geometry are not certified."},
   {"6f69a840","Payoff247: public Stop captures retain two bridge calls per command and the independent651590 unit exclusion. This chunk integrates05ca50's bridge lifetime; it does not certify the entire public notification/duplicate-call chronology or close MAP-04.2."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f05ca50","6f05bd30","6f171340","6f170080","6f69a840","6f654060"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
