// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Captain291Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f9bb750","TownSpot_Lookup","Payoff291: ECX is the global zero-based index; EAX loads table d77e74[index]. CTownSpot records are map-owned and shared across Town owners; indexes remain append-only until map teardown."},
 {"6f9bc1c0","TownSpot_Initialize","Payoff291: ECX spot; stack4 index, stack8/12 raw scalar pointers; RET12. Index24 and FloatMini attack28/30, defense38/40 receive the same initial XY. Canonical building48, mine54 and overlay60 remain separate."},
 {"6f9bfe80","TownSpot_RefreshDefaultCaptainHomes","Payoff291: ECX CTownSpot, plain RET. Resolve mine54 and building48. No live anchor retains defaults; one anchor sets both homes to anchor XY. Two anchors: defense=mine+(building-mine)*0.5; attack=building+((deltaXY*300)/sqrt((dx*dx+dy*dy)+dz*dz)). Preserve software scalar order and full XYZ length. Runtime d3c7d8=300. Two read-only repeats/control agree151 public markers; nine live refreshes per repeat match unmodified binary. Seven extra controlled pre-call cases cover absent/coincident/nonplanar/subpixel/large coordinates; they are not public placement journeys."},
 {"6f9bd2a0","TownSpot_CreateAtPoint","Payoff291: Allocates a new global CTownSpot, initializes supplied scalar XY and appends its stable index. Empty CreateCaptains uses assigned start-location index, not player-number slot or arbitrary0."},
 {"6f9bd390","TownSpot_CreateForAnchor","Payoff291: Selector0 records building48; selector1 records mine54. Query Unit bridge XYZ, create the shared point record and register the canonical anchor."},
 {"6f9bf6c0","TownSpot_SetBuildingAnchor","Payoff291: Unsubscribe previous building, assign canonical identity48/4c, subscribe target lifecycle and refresh default homes. Does not overwrite authored Captain homes directly."},
 {"6f9b7d20","TownSpot_SetMineAnchor","Payoff291: Mine54/58 admission subscribes anchor lifecycle and refreshes defaults. Existing different live mine uses224310 identity write and returns; do not invent recoverable allocation/retirement semantics."},
 {"6f9b7c10","TownSpot_RegisterMine","Payoff291: Resource entry uses9ba790(unit,0,0), assigns an available mine anchor or creates selector1 CTownSpot. Natural neutral mines are map-global, not owned Town buildings."},
 {"6f9ba5d0","TownSpot_FindForBuilding","Payoff291: Global encounter-order scan of live mine54 anchors. Real owner-excluding coarse distance strictly below80; initial work150 tightens to last query work+50. Equal distances retain first; supplied point and live unit are separate entry domains."},
 {"6f9ba790","TownSpot_FindForUnit","Payoff291: Global encounter-order scan, live mine then building then stored defense XY. Strict distance70, initial work5000 tightened to work+100, zero terminates; resource mode excludes live overlay60. Optional geometric fallback is a separate producer, not certified by291."},
 {"6f9ccb60","TownAI_AppendSpot","Payoff291: ECX Town, stack4 global spot index, RET4. Deduplicate four ordered slots1b8..1c4, append first -1; full list remains unchanged. Engine indexes are1-based with0 absent."},
 {"6f9c01f0","TownAI_AdmitSpot","Payoff291: ECX Town, stack4 nullable Unit, stack8 nullable XYZ. Find nearby mine using9ba5d0; found Unit replaces building anchor. Otherwise create selector0 anchor or supplied point, then append owner slot. Nested anchor admission remains sequential."},
 {"6f9c4a20","TownAI_SelectOwnedUnitSpot","Payoff291: Worker/ordinary building selection scans four owner spots, preferring building48 then mine54. First queried anchor wins even UINTMAX; strictly lower later distances win. Initial work5000 tightens to work+100. No usable anchor returns first owner slot; no owner slot uses global selection."},
 {"6f9c4280","TownAI_PruneUnusedSpots","Payoff291: Scan current owned Unit AI association48, retain referenced owner slots and compact order; invoke9cc3e0 policy inverse when slots move. Engine291 derives per-player spot reference counts, O(4) owner pruning. Remaining policy inverse is not closed by291."},
 {"6f9bd610","TownSpot_OnAnchorEvent","Payoff291: d01a0/d01a4 unregister anchor event subscription; mine loss clears overlay60, overlay loss clears60, otherwise clear building48. Mine identity invalidates through its own lifecycle. No immediate default refresh; subsequent Reset refresh is observable."},
 {"6f9c5360","CaptainAI_CreateCaptains","Payoff291: 291 supersedes earlier unimplemented-default note. If owner first slot is -1, obtain assigned player start index+274 from map start table stride2c, admit that point through9c01f0. First shared CTownSpot supplies distinct attack2c/34 and defense3c/44. Create real actors via9c5270: attack state2 identity2ac/2b0, defense state1 identity2b8/2bc. Placement competition is observable; equal defaults do not imply equal admitted physical poses."},
 {"6f9c9c10","TownAI_ResetCaptainLocations","Payoff291: 291 supersedes290 audit-only scope. Resolve first owner CTownSpot; refresh live anchor defaults, SetSelectedHomes1 then Teleport attack, SetSelectedHomes2 then Teleport defense. Authored current homes are not the defaults. Seven resets per repeat preserve live member trajectories across the unhooked control; actual home callbacks and ordinary placement execute at each original boundary."},
 {"6f9bee60","Jass_ResetCaptainLocs","Payoff291: Zero-argument current AI context resolves Town then tail9c9c10.291 integrates shared default producer and actual ordered actor reset; no claim about broader combat/default timer policies."},
 {"6f3612d0","Math_VectorLength3","Payoff291: Original software scalar length sqrt((x*x+y*y)+z*z), used by CTownSpot refresh. Retain Z even though output homes are XY; nonplanar controlled cases distinguish2D simplification."},
 {"6f9cc6b0","TownAI_SelectInitialSpot","Payoff291: Audit: absent first owner slot chooses newest owned worker immediately; otherwise oldest structure then oldest nonstructure. Timer producer remains unported291; do not confuse CreateCaptains assigned-start fallback with this independent policy."},
 {"6f9c0280","TownAI_AdmitOverlaySpot","Payoff291: Audit: special ugol producer finds existing mine spot; overlay60 identity and owner policy remain outside291. Native special classification is not ordinary gold-return admission."}
 };
 public void run()throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh export required");
  var output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  StringBuilder s=new StringBuilder();
  for(var row:NOTES){var a=toAddr(row[0]);var f=getFunctionAt(a);
   if(f==null){disassemble(a);f=createFunction(a,row[1]);}if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   s.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');s.append("COMMENT ").append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();s.append(i.getAddress()).append('|');for(byte b:i.getBytes())s.append(String.format("%02x",b&255));s.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(a))s.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(output,s);println("Saved "+output);
 }
}
