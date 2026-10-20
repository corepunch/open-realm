// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Work256Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f5fb940","CAbilityMove_ValidateTarget","Payoff256 TARGET-03.2 policy closure: retains the distinct deadDD, absentAA, cargoA9 and mode4/flags0 visibility contracts. Frozen17 loss scenes and5 reacquisition scenes are linked to production regressions by retail-target-policies256. Pausing a target is not a validation failure or a TargetLost producer; full numerical trajectory gates remain separately scoped."},
 {"6f23a760","PathGroup_IsTargetNotVisible","Payoff256: repeated original fog_shared scene2 confirms afterUnits FOGGED overrides target-owner shared vision. Hidden visits retain cached destination while countdown advances; a newly visible visit resets unseen without forcing a sample. Engine covers Smart approach/persistent, Attack and Move, including saved hidden episodes."},
 {"6f169680","PathGroup_UpdateTargetRefresh","Payoff256: frozen reacquire scenes0..4 distinguish remaining countdown2/0/0/0/3 at the first visible visit. Actual production owners now regress all five policy compositions with14/40/26/13/13 hidden visits and cold saves. Captured native scene timings remain immutable; the minimal engine arenas test policy/route ownership independently of full geometry."},
 {"6f697770","Unit_BeginScriptedPause","Payoff256: original TARGET-03.2 pause scene12 retains the follower Smart head and emits no TargetLost. Engine regression pauses a moving target through public JASS, cold-saves it, checks frozen target pose and retained follower identity, then resumes through S_RunMoveTimers. Raw repeated captures show a fresh target point group at9491 after unpause9490, before follower evaluation. Engine resume now creates this physical owner instead of leaving entity-order fallback. No user-issued order event is synthesized."},
 {"6f5ff490","CAbilityMove_OnTargetLost","Payoff256: original order_invisible scene14 accepts Smart during the authored fade, cancels at invisibility publication, does not resume on UnitRemoveAbility, and retains the explicit second Smart. Engine composes this with nonstock2.75 fade through real primary timers. Original approach point-reissue evidence216 remains separate and unchanged."},
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
