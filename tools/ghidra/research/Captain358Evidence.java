// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain358Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh output required");
  Path output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  StringBuilder text=new StringBuilder();
  String note="Payoff358 static call-site correction:9d8460 receives combat contact from both fresh Attack acquisition49419b and Town AI notification69c835. It is not damage-only. Far home, no retreat, town flee enabled, state!=1 and flags1000 clear delegates9d08e0. Otherwise c0>0 engages9cf410; c0<=0 invokes9d2670 without setting retreat2. Near home clears retreat2 only when c4>=bc/2, then engages. Two fresh public Sorceress melee repeats and observer-free control retain439 identical markers. Their c0zero combat contact replaces south200 with north home500, flags1001 unchanged. Engine public-frame/save regression is separate; no complete combat target, trajectory or retail-save parity claim.";
  var contact=getFunctionAt(toAddr("6f9d8460"));String old=contact.getComment()==null?"":contact.getComment();if(!old.contains(note))contact.setComment(old+"\n"+note);
  for(String va:new String[]{"6f9d8460","6f69c810","6f494030","6f9d0650","6f9d2670"}) {
   var f=getFunctionContaining(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
