// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Attack306Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f49a240","Attack_BeginTargetChase","Payoff306: two complete read-only public Attack repeats and unhooked control match349 markers. Ground Footman/Farm and flying Gryphon/Footman use nonpersistent target owners; later facing requests via49a4e9 use FLT_MAX and physical1200. There is no flying-source or structure-target exclusion. Initial approach is dispatched by Move_HandleTargetTask, not necessarily this continuation. Stock Footman/tree negative control uses physical point tasks instead, so do not generalize this evidence to destructibles."},
   {"6f5fc640","Move_BeginTargetTaskRequest","Payoff306: initial out-of-range Footman/Farm captures90-world range, a nonnull target footprint region and fine6.03125; Gryphon/Footman captures450-world, null request region and fine15.28125. Both use persistent0/subscription0. Close facing approaches captureFLT_MAX. Engine Attack now shares the existing physical owner for structures/flyers, retaining public Attack, cancellation and saved receiver; no new polling task or routing algorithm."},
   {"6f5ff8b0","Move_HandleTargetTask","Payoff306: public Attack first approaches for ground/Farm and flying/Footman enter5fc640 via5ffa99 with captured weapon range90/450, persistent0 and subscription0. Initial close-facing approaches useFLT_MAX. Two observer repeats retain exact public samples versus unhooked control, including pause/resume and Stop. Stock Footman/tree produces point physical groups with no target-request entries; keep that negative control separate. Full Attack damage/backswing timing remains outside this slice."}
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
