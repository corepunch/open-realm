// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Removal304Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f69c510","Unit_CommitRemovalInactivity","Payoff304 MAP-04.2 correction: new complete read-only direct idle/moving/paused and nested issued-Stop removal captures extend through physical destruction. Each has one5fc18a Move first-disable recovery inside69c510 AFTER RemoveUnit returns. Earlier Stop284 window ended at public return and could not establish deferred absence. Commit dead flag/health0 before availability retirement; run the counted owner before separation inverse and physical storage free."},
   {"6f6744d0","Unit_FinalizeDeathStateAndPathing","Payoff304 MAP-04.2:69c510 invokes this with death notification argument0.48c8c0(1,1,0,0) acquires first retirement availability contribution and invokes Move5fc120; final removal does not emit a public death event. Keep semantic availability retirement separate from later destructor."},
   {"6f4076e0","Ability_AddDisableContribution","Payoff304 MAP-04.2: direct and nested removal repeats observe Move3c/40/44 as1/0/0 at the final recovery and1/1/0 after inactivity finalization. Paused removal cancels previous suspension first; retirement still crosses nonpositive-to-positive and invokes the owner. Do not suppress this callback merely because storage destruction follows."},
   {"6f5fc120","CAbilityMove_CancelOwner","Payoff304 MAP-04.2 correction: complete six-scene removal repeats observe this owner once during69c510 after return, including nested Stop callback RemoveUnit. The original return-window Stop284 stage counts remain valid and unchanged. Deferred first-disable must execute before fine/mover storage is destroyed."}
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
