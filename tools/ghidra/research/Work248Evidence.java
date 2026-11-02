// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Path;
public class Work248Evidence extends GhidraScript {
 public void run()throws Exception {
  String[][] notes={
   {"6f169620","Payoff248: canonical attachment deduplicates mover identity and keeps a separate readiness slot. Pending attachments are not physical members. Engine selected point packets now use bounded canonical candidate/readiness records instead of allocating physical owners during preparation; stable physical storage begins only at successful publication."},
   {"6f89cd10","Payoff248: readiness marks the canonical slot then immediately calls wrapper virtual+c (249b60 ->16bcf0). Two complete read-only Alt+Shift repeats retain six publication attempts each, including one primary busy return; native readiness/drop kernel repeats four cases at outer exclusion depths0/3, with observer-free controls."},
   {"6f169c50","Payoff248: acquire holds all live canonical attachments, including pending rows. Native kernel and two live repeats sample before/after acquisition and release. Primary busy mover keeps its motion bit20000000; scopes add/subtract exactly one, without booleanizing flags or changing coarse occupancy. Engine selected publication owns these balanced fine holds."},
   {"6f169d60","Payoff248: release re-resolves candidate identity/current mover fine object; consumed readiness is irrelevant. Four native cases preserve all outer depths across busy/success/drop; both live repeats balance24 scope boundaries. No arbitrary notification reentrancy or target-region lifetime is certified by this selected-point witness."},
   {"6f89ca80","Payoff248: drop invalidates the canonical candidate slot then attempts publication. Busy selected Shift recipient is removed before the ready primary Footman publishes. Engine drops rejected/busy candidates rather than treating them as physical members, preserving holes and attachment order."},
   {"6f16bcf0","Payoff248: canonical attachments, readiness and physical cohorts now have distinct engine storage. Physical allocation occurs after pending admission clears; the existing cached-hierarchy recursive partition remains shared. Original bytes and frozen Work235/238/240/241/243/244 trajectories are preserved. Captain/target and full queued reconstruction scopes remain GROUP-04.6/MAP-04.2."}
  };
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);if(row[0].equals("6f89ca80"))f.setName("MoveRequest_DropMoverAndPublish",SourceType.USER_DEFINED);String old=f.getComment();if(old==null)old="";if(!old.contains(row[1]))f.setComment(old+"\n"+row[1]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f169620","6f16d600","6f16d850","6f89cd10","6f249b60","6f169c50","6f169d60","6f89ca80","6f16bcf0","6f16bdb0","6f16b7b0"}){
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
