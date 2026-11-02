// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain358AcquisitionEvidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh output required");
  Path output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  StringBuilder text=new StringBuilder();
  String note="Payoff358 acquisition continuation: read-only Sorceress AI-point source contact1557 is produced by independent493ff0 d01c1, with d6bf40 idle0.5 and d6bf44 active1.0.493d20 seeds retained unsigned64 priority and493b20 uses strict committed-distance ties; nearest-only generic AI policy is not equivalent.4943f0 publishes Attack/state1/target before fresh9d8460, which returns c0zero Captain home500 and reissues members: timerleave is state7/targetnone. Later1570 publications are zero-damage AllyCombatAlert packets flags4 via49bb50/49bbd2, not availability or a second fresh scan; removing Aslo from all7 units leaves this intact. Clean public repeats/control379 markers document instrumentation parity, not complete actor trajectory replay. Engine Save205 adds the missing d0014 Move-owned acquisition producer with saved Attack primary request, pause/execution-disable admission, speed-cap then Attack then fresh Captain notification and incarnation guards. Other legacy scan owners, full candidate/neutral/event policies and cross-owner callback order remain existing GROUP-03.4.7.3.";
  var contact=getFunctionAt(toAddr("6f493ff0"));String old=contact.getComment()==null?"":contact.getComment();if(!old.contains(note))contact.setComment(old+"\n"+note);
  text.append(note).append('\n');
  for(String va:new String[]{"6f493ff0","6f493d20","6f493b20","6f493c80","6f493cb0","6f4943f0","6f498e50","6f4959f0","6f9d8460","6f05ab00"}) {
   var f=getFunctionContaining(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
