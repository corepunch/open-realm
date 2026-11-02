// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Speed314Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f698af0","Unit_SetMoveSpeed","Payoff314 NUM-03.2: Public SetUnitMoveSpeed resolves the Move ability and delegates raw input to5fee60; missing Move is a no-op. Payoff314 uses actual JASS setters with DefaultFlyHeight-produced exceptional W3U cells, not injected memory."},
   {"6f5fee60","CAbilityMove_AssignSpeed","Payoff314 NUM-03.2: Assignment retains raw base-speed word in the embedded scalar, then publishes through6005d0. Actual qNaN7fc00001, sNaN7fa00001 and negative qNaNffc00001 survive public setter/getter exactly across foot, fly, amphibious, hover and float."},
   {"6f5fc900","CAbilityMove_ComputeEffectiveSpeed","Payoff314 NUM-03.2: Effective speed uses software Add(base70,flat bonus) and software Multiply(retained factor78), followed by COMISS bounds. Unordered words remain unchanged; infinities and finite extrema clamp to authored min173/max389 in public Payoff314 actors. Do not quiet signaling NaN through hardware identity multiplications. Special speed flags/effect combinations are outside this matrix."},
   {"6f16b060","PathGroup_SelectSpeedCap","Payoff314 NUM-03.2: Cap selection compares mover88 AFTER software conversion to fine units. Raw positive/negative NaN world speeds convert to finite fine scalars; world-unit comparisons do not preserve their ordering. Sentinel is fine FLT_MAX. Payoff314 singleton public Move captures do not establish exceptional shared-generation behavior."},
   {"6f16b5c0","PathGroup_CommitMemberMovementWithCap","Payoff314 NUM-03.2: Requested member20 versus cap comparison is in FINE units. Negative qNaN world speed becomes a negative finite request; selecting cap with a world NaN comparison incorrectly takes the positive branch. Payoff314 observes stationary commits, finite restoration, arrival and Stop in five movement families."},
   {"6f16fe20","Mover_CommitSpeedAndHeading","Payoff314 NUM-03.2: Positive FINE requested speed computes desired velocity and lets velocity-derived facing persist; nonpositive request explicitly commits supplied heading even at zero velocity. Negative qNaN world request becomes negative fine and takes the latter branch. Positive NaN-derived request is positive fine but its extreme arithmetic yields stationary motion in Payoff314; finite270 restoration resumes Move."},
   {"6f203d30","Jass_GetUnitMoveSpeed","Payoff314 NUM-03.2: Payoff314 public getter returns raw7fc00001/7fa00001/ffc00001 after actual exceptional setters. Authored min/max173/389 clamp infinities and finite extrema. Two observer repeats plus unhooked control per movement family; only scoped actor action/commit streams are repeat-identical, unrelated transient mover events remain in raw captures."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c!=null)c=c.replace("conversion at058f11", "conversion at058eef..058f09");f.setComment(c==null||!c.contains(row[2]) ? (c==null?"":c+"\n")+row[2] : c);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
