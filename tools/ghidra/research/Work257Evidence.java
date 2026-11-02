// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work257Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f69a840","Unit_StopWithRecoveryAndSupport","Payoff257: nonstructure support recovery owns virtualD0(1) ->05ca50 ->virtualD0(0). Two read-only repeats of unchanged Work247 map retain111 normalized scope events each and all14 public markers. Unit, bridge and embedded recovery depths are independent: bridge entry2, footprint3, bridge release1, then unit release0. Engine now wraps its existing bridge recovery in this unit scope; previous247 kernel/pose fixtures remain unchanged. Duplicate public cleanup and notification reentrancy remain open."},
 {"6f651590","Unit_ToggleSpatialExclusion","Payoff257: canonical wrapper gate resolves identity, requires tag2b61676c and +20==0; then original6864d0 returns embedded bridge+164.05bd30 toggles mover first, then063d10 toggles all current widget regions. Both calls re-resolve at release; no captured region list is retained. Complete original gate kernel covers16 supplied canonical/tag/liveness/list/depth cases plus controls. Fixture uses a canonical mover allocation with supplied unit tag; no original Unit factory is claimed."},
 {"6f063d10","WidgetList_ToggleSpatialExclusion","Payoff257: all nonnull region pointers toggle in array order; null entries skip and aliases toggle once per occurrence. Original null/alias list0x10000003 ->0x10000005 ->0x10000003 executes unchanged. Engine unit scope walks existing Move-owned four-region collection without allocation/republication; frozen offsets and original lane defaults unchanged. Engine Stop/Move regression uses four real authored-pixel regions and cold save and outer depths0/3."},
 {"6f6864d0","Unit_GetMoverBridge","Payoff257: original unit virtualB8 getter is LEA EAX,[ECX+164];RET. Native unit-exclusion kernel supplies a vtable pointing here rather than replacing the virtual callback. Unit wrapper/canonical tag are explicitly supplied fixture inputs."},
 {"6f68c190","Unit_IsStructureForSupportRecovery","Payoff257: support branch tests unit+5c bit10000 and parameter1 with signed low-byte bit80/08000000 override. Static producer distinction retained; this chunk ports the demonstrated ordinary nonstructure outer scope. No synthetic structure-state matrix is claimed as public evidence."},
 };
 public void run()throws Exception {
  if(getScriptArgs().length!=1)throw new Exception("new output required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
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
