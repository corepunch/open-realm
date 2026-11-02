// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain378DeathEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll apply/read fresh-output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Captain378: Unit_BeginDeathTasks commits life zero and the dead flag, dispatches TargetLost through 651010, then dispatches the separate death event through 650d80 before cancelling the old user order and physical task. The engine now follows that frontier with incarnation guards. Later Move death cleanup preserves generic owner notifications without publishing TargetLost again. Ordinary Remove preserves its entity-count snapshot from before Lost callbacks. The precise frontier regression first passed 46/64 assertions per edition, then 84/84 after the previously missing death callback began executing its additional checks. Final acceptance passes 8 tests/376 assertions per edition and verifies 2287 original guards. Format 214 rejects older subscription meanings before world restoration. Public script death-family ordering and unrelated death cleanup remain separate scope.";
  String[] addresses={"6f679bc0","6f650d80","6f652cb0"};
  StringBuilder text=new StringBuilder(note+"\n");
  for(String address:addresses){var f=getFunctionContaining(toAddr(address));if(f==null)throw new Exception(address);
   if(args[0].equals("apply")){String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var reference:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(reference).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
