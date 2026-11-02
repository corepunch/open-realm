// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain369SelectionEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff369 GROUP-03.4.7.3: CaptainVsPlayer public callback9b8b90 and CaptainVsUnits9b8c20 reach9cc850 with raw flag80 input0 and flag4 input1/0. SuicidePlayer registration9be64a binds9c02d0(flag80=1); registered SuicidePlayerUnits9c0360 usesflag80=0.9d7240 stores requested player130, updates80/4, resets12identity/score buckets and appends enumerated candidates through9cf8a0; it arms repeating d01c2 on +98 at rawperiod3dcccccd. Two owned immediate-Clear observations plus unhooked control retain281 identical publicmarkers. At1357 Begin arms selection deadline41219986/serial255, then Clear removes its request/receiver while periodic+84 deadline and serial49 are unchanged. No selection callback occurs after cancellation. Separate ordinary selection probe1357->1360 processes candidate1343 into unreachable-style bucket5/score9696 and returns without a chosen target. This is not full candidate-ranking/transport policy or an engine parity claim; Save209 owner integration remains pending.";
  String[][] names={{"6f9d7240","CaptainAI_BeginTargetSelection"},{"6f9d5e00","CaptainAI_OnTargetSelectionTick"},{"6f9cf8a0","CaptainAI_QueueSelectionCandidate"}};
  if(args[0].equals("apply"))for(String[] row:names) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!f.getName().equals(row[1]))throw new Exception("unexpected name "+f.getName());
   String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9d7240","6f9cc850","6f9d5e00","6f9cf8a0","6f9b8b90","6f9b8c20","6f9c02d0","6f9c0360","6f0606c0"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
