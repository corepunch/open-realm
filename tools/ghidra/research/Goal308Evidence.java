// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Goal308Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f04c330","Widget_ClampWorldPoint","Payoff308: public SetUnitPosition clamps each world axis before virtual admission, including SetUnitPathing(false). Flat0..2048 arena clamps requests to0..2016 before the footprint/terrain-level search; west/north edge admission can subsequently move both axes by the original ring tie order. Preserve queryzero footprint/bounds checks; it is not an unclamped coordinate write. Axis setters remain separate producers."},
   {"6f653510","Widget_WritePositionAfterAdmission","Payoff308: Unit public2155c0 -> virtual180/698050 reaches this shared widget writer. Copies requested scalar XY, calls04c330 before optional virtualdc admission, optionally snaps a path texture, then writes the admitted position through05c200. Public unit placement must use this clamp before searching; merely finding a legal point from the original outside request changes ring distance/tie order. Ordinary point Move destinations are not processed by this clamp."},
   {"6f698050","Unit_SetPositionWithAdmission","Payoff308: public unit position delegates shared Widget_WritePositionAfterAdmission after support-policy setup. Its public Stop already ran in2155c0. The default unit path and queryzero both keep the outer world clamp; preserve this order independently of the inner terrain/object query."},
   {"6f165ae0","Path_Advance","Payoff308: twelve public goal scenes record complete advance returns, retained fine index/count/flags/delay/request and held output, covering east/north/diagonal outside clicks, unit/Farm overlaps, Farm target versus point, target removal, flyer and queryzero. These clicks retain original coordinates; do not apply the widget placement clamp to movement destinations."},
   {"6f166e90","Path_RequestFineRoute","Payoff308: first local requests for all twelve public goal scenes have25 points/index0. Point goals in solid units/Farms retain the clicked goal through partial legs; target Farm uses3.21875 fine arrival range and a target identity. Original return codes/index/flags are frozen separately from the complete physical motion; no host-path approximation or perimeter snapping."},
   {"6f5ff490","CAbilityMove_OnTargetLost","Payoff308: public removal of a Farm during its nonpersistent target approach reissues a complete point task at the retained public order point. The actual successor binds a new physical singleton synchronously at owner2724; it is not only a walker callback/waypoint replacement. Persistent Footman Follow has no successor and cancels at2924. Engine must finish internal point admission through the same physical-owner stage used by ordinary accepted orders, without emitting an extra issued-order event.12-scene motion plus the removal clocks are frozen."},
   {"6f05a5c0","MoveBridge_StartTarget","Payoff308: distant Footman target uses persistent range11.3125, Farm target uses one-shot3.21875. Removing the Farm during approach reissues point Move at the retained original order point; removing the Footman cancels persistent Follow synchronously. Paused colocated occupants remain distinct from the captured target exclusion."}
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
