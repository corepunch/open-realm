// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain369PrimaryLossEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff369 GROUP-03.4.7.3 primary loss: two RemoveUnit observations and unhooked control retain307 identical publicmarkers. Initial Begin1357 selects primary atPoll1360/state3. RemoveUnit1424 invokes9d2700 then Begin fromreturn9d27b1, retaining state3 and rearming independent+98 serial319/deadline4141997a; empty queryPoll1427 cancels. 9d2700 resolves active follow into primary or invalid, refreshes policy1/range200, setsflags2000 and only rearms savedrequestedplayer130/raw80/raw4 when state3 and GetPrimary9d2300 is null. Native9d7760 reevaluates home before role matching, calls this inverse for primary loss and follow-primary loss, whereas backup-primary loss only copies follow. Immutable compiled78838 production-module TEST plugin records4 genuine behavior failures among16 assertions in each edition;447DWARF ABIguards and loaded-library path guard pass. Ordinary source regression and port await combined verification; full Town preferred-target override remains open.";
  String[][] names={{"6f9d2700","CaptainAI_RecoverPrimaryTarget"}};
  if(args[0].equals("apply"))for(String[] row:names) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!f.getName().equals(row[1]))throw new Exception("unexpected name "+f.getName());
   String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9d2700","6f9d7760","6f9c7b20"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
