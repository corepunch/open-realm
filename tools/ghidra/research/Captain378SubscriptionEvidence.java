// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain378SubscriptionEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll apply/read fresh-output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Captain378: roster attach 9cf680 enables death and owner subscriptions but leaves TargetLost disabled; retained target roles enable all three events. Shared event entries preserve the union and each original FIFO rank through role changes and save/load. Two clean retail observations and an unhooked control match 678 public markers. All 1264 semantic rows retain identical literal sequence values 2 through 1265, clocks, flags, call sites and canonical identities after only one raw Captain pointer alias. Cargo loading preserves membership; return recenters the Captain; death withdraws the member. The original three tests failed 28 behavior assertions; the precise early death frontier separately failed 18. Final acceptance passes 8 tests/376 assertions per edition, plus 323 save and order-lifecycle tests/87770 assertions per edition. The oracle verifies 2287 original guards. Format 214 rejects older subscription meanings before world restoration. Wider Town policy and the precise public script death-family bridge remain separate scope.";
  String[] addresses={"6f9cf680","6f9d5610","6f9d4a50","6f9cf410","6f9d5e00","6f9d7760","6f9d04a0"};
  StringBuilder text=new StringBuilder(note+"\n");
  for(String address:addresses){var f=getFunctionAt(toAddr(address));if(f==null)throw new Exception(address);
   if(args[0].equals("apply")){String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);}
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var reference:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(reference).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
