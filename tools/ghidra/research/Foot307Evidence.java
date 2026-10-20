// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Foot307Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f170080","Mover_RecoverEmbeddedFinePoint","Payoff307: repeated six-scene public blocked-start Move controls show recovery before point/target admission. Clear self-only288/288 remains; an unrelated Footman at800/800 admits784/752; Farm at768/768 admits720/688, including fractional embedded799.25/768. Targeting the same blocking unit/Farm does not suppress this bridge recovery. Targets already within the captured arrival range then use facing-only groups without any fine route. Preserve the five-attempt placement order; final fine-route exclusion is a later scope."},
   {"6f165ae0","Path_Advance","Payoff307: public blocked-start first output is the retained route goal for clear self-only scene0 (index0), but a local next waypoint for unrelated unit/building starts (indices23/25). Same-object target scenes3/4 are already in range after recovery and construct no fine route; do not manufacture a first point for them. Two complete read-only repeats retain the full advance/commit stream."},
   {"6f166e90","Path_RequestFineRoute","Payoff307: six public embedded-start scenes retain seven complete fine returns across the run. First unrelated Footman route has25 points/index23; Farm and fractional Farm starts have27/index25. Clear self-only has25/index0 with observed-obstruction clear. Targeting the start blocker produces no fine request after the earlier bridge relocation and range decision. The output arrays and first Path_Advance result are frozen separately from scalar motion."},
   {"6f05b970","MoveBridge_StartPoint","Payoff307: public ordinary point Move recovers embedded source before binding the physical mover, retaining requested destination1536/800. The immediate public return exposes784/752 for a unit blocker and720/688 for a Farm blocker. Recovery is not deferred to first owner execution."},
   {"6f05a5c0","MoveBridge_StartTarget","Payoff307: public target Move to the same embedded Footman/Farm first performs the same recovery as point Move. Its admitted pose is784/752 or720/688, then the captured target-range/facing decision requires no fine route. Target exclusion during a later fine search must not be applied to initial embedded recovery."}
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
