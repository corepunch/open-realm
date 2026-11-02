// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain368ClockEvidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(getScriptArgs()[0]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff368 static correction: CaptainAttack9d1680 and ClearCaptainTargets9d0f20 cancel the EventClock control at Captain+98 via0606c0. Older CaptainAttack comment calling this a list clear is inaccurate. It is separate from periodic d01c1 control+84, whose receiver LEA is9d3278.9d7240 arms+98 with periodic d01c2 through0608d0; callers9cc850 and primary-target recovery9d2700. Dispatch9d4aa0 maps d01c2 to9d5e00 target selection. This establishes control ownership/cancellation, not runtime target-search policy or completed engine parity.";
  var method=getFunctionAt(toAddr("6f9d1680"));String old=method.getComment()==null?"":method.getComment();if(!old.contains(note))method.setComment(old+"\n"+note);
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9d1680","6f9d2f90","6f0606c0","6f9d7240","6f9cc850","6f9d4aa0"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
