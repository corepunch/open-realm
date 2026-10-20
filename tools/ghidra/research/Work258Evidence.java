// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work258Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f69a840","Unit_StopWithRecoveryAndSupport","Payoff258: nonstructure predicate68c190(1)==0 selects unit exclusion plus654060 recovery; structure branch passes callback/contextNULL to05ca50. Two public four-form Stop repeats plus unhooked control retain15 markers and68 scope events: Footman and uprooted Tree enter twice, Barracks and rooted Tree enter zero times. Engine structure Stop skips Move cleanup; direct internal structure support cancels without embedded placement. No extra public duplicate callbacks are synthesized."},
 {"6f68c190","Unit_IsStructureForSupportRecovery","Payoff258: original16-case predicate kernel verifies bit10000 AND (mode0 OR signed-low-byte>=0 OR bit08000000). Uprooted etol public Stop shows5c=00019281: authored10000 survives but low-byte80 selects ordinary recovery. Engine uses current EF_BUILDING form through G_UnitIsStructure, never rawcode building identity; rooted-to-uprooted actual ability regression retains authored building identity."},
 {"6f651590","Unit_ToggleSpatialExclusion","Payoff258: public uprooted etol Stop retains three widget regions despite mobile form. Both original repeats show their10000000 ->10000001 ->10000000 transitions around each bridge recovery; these real public regions strengthen257's original list kernel and engine four-region composition. No callback or region storage replacement is injected."},
 {"6f171340","Mover_StopWithRecovery","Payoff258: callbackNULL bypasses170080 but still integrates/stops velocity, detaches physical owner and invalidates retained path. All eight no_callback original247 rows and their observer-free controls remain unchanged; new structure engine tests cover clear/blocked/exhausted terrain, four collision classes, internal/public entries and cold load. Prior numeric/pose fixtures remain immutable."},
 {"6f05ca50","MoverBridge_StopWithRecovery","Payoff258: bridge exclusion still brackets171340 when invoked with aNULL callback. Do not implement this as an early return that leaves a physical task or path active. Ordinary rooted public Stop has no Move cleanup invocation; that public admission gate belongs to Stop, independently of this bridge helper."},
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
