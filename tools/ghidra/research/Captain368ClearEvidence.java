// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain368ClearEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll") || args.length!=2 || (!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("original binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff368 GROUP-03.4.7.3: public ClearCaptainTargets reaches9d0f20 directly. This commits state2 and cancels its own event-clock request, then processes additional, follow and resolved primary identities in that order through9d7760 semantic removal. It does not unconditionally issue GoHome or overwrite a point request. Two fresh read-only pointclear captures plus observer-free control have233 identical public markers. Atcounter1390 request differs from home, all three target identities are null and the full observed Captain state before/after is identical; actor/member orders continue. Separate source-follow cancellation and mixed-owner callback ordering require their own tests and are not implied by this no-target witness. Existing engine Clear goal=home was therefore incorrect; a production regression is added before implementation.";
  var method=getFunctionAt(toAddr("6f9d0f20"));if(method==null)throw new Exception("missing method");
  if(args[0].equals("apply")) {
   if(method.getName().equals("FUN_6f9d0f20"))method.setName("CaptainAI_ClearTargets",ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!method.getName().equals("CaptainAI_ClearTargets"))throw new Exception("unexpected method name");
   String old=method.getComment()==null?"":method.getComment();if(!old.contains(note))method.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder();text.append(note).append('\n');
  for(String va:new String[]{"6f9d0f20","6f9d2300","6f9d7760","6f9d2700","6f9d6f20","6f9d4600"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
