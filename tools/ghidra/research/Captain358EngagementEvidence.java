// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain358EngagementEvidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh output required");
  Path output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  StringBuilder text=new StringBuilder();
  String note="Payoff358 continuation: two fresh near-home hostile-contact captures plus observer-free control retain378 identical public markers. At counter1424 native9cf410 rotates source into follow114 via9d6f20, refreshes policy0/range200 through9d4600, updates active54/5c to source while preserving retained d4/dc home and e4 range200. Counter1609 actor completion refreshes the live source again; it is not a permanently frozen point. Negative-mana-c0 repeats/control retain353 public markers and signed c0=-1 on removal, with home500/no-retreat at contact1557. Two additional source RemoveUnit repeats and observer-free control retain380 identical markers:9d7760 at1624 reevaluates, rotates null follow114, restores retained point with9d4600 policy1/range200, roster count6 andc0zero unchanged, flags1001->3001. Common contact/target admission and retained point are scoped separately from overflow/hero/allied support, cross-owner subscription order and retail save parity.";
  var contact=getFunctionAt(toAddr("6f9d8460"));String old=contact.getComment()==null?"":contact.getComment();if(!old.contains(note))contact.setComment(old+"\n"+note);
  for(String va:new String[]{"6f9cf410","6f9d0d40","6f9d4600","6f9d6f20","6f9d70e0","6f9d7760","6f9d02c0","6f66ec40","6f9d0650"}) {
   var f=getFunctionContaining(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
