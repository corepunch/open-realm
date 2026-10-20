// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Captain373WorldQueryEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff373 GROUP-03.4.7.3 world query: WidgetQuery default excludes Unit5c118 (8 world retirement,10 loaded cargo,100 death); structure token2 selects Unit_IsStructure. Public ShowUnit hiddenhtow two observations/control match332markers and source callbacks include onlyugol+hbar. Public AOwk Hpal three-phase Show survey two observations/control match387markers: BOwk1 andUnit1141 persist, constructor9cf8a0 includes Hpal at1357/1640 but not hiddenphase; Showfalse1457 sets5c8 andWidget20bit1, Showtrue1607 clears8/restoresWidget20 while114stays1. Additional actual651010 hook with387publicexact records WindWalk caller68b7d8 whileworldpresent, thenShow caller688373 AFTERretirementflags published. Ordinary6test139assertbaseline125/139 each; additional39assertcoexistence has30guards pass/9behaviorfail includingphysicalMovepriority/save/renderowner. Save212 engineport firstgreen6tests139assertions each; strict3611originalguards,7observations3controls1050publicmarkers. Independent script_world_hidden owner published before TargetLost, preserved through cold save and inverted without removing active invisibility. No full-map journey parity claim.";
  String[][] functions={{"6f688300","Unit_RetireWorldPresence"},{"6f699c00","Unit_RestoreWorldPresence"},{"6f651010","Unit_DispatchTargetLost"},{"6f68c320","WidgetQuery_FilterUnitFlagWords"},{"6f68c3f0","WidgetQuery_PredicateSelector2"},{"6f9bcb80","TownRegionGraph_MarkStructureSource"},{"6f9cf8a0","CaptainAI_QueueSelectionCandidate"},{"6f9cc850","TownAI_BeginAttackCaptainSelection"},{"6f9c02d0","JassAI_SuicidePlayer_BeginSelection"}};
  StringBuilder text=new StringBuilder(note+"\n");
  for(String[] row:functions){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[0].equals("apply")){if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var reference:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(reference).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
