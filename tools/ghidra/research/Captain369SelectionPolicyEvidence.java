// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain369SelectionPolicyEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff369 native selection policy: repeated reachable CaptainVsPlayer observations plus unhooked control retain282 exact publicmarkers; Begin1357 arms only selection+98, poll1360 selects primary1343/state3/flags1005 and publishes(-1936,-1424). Reachable unit bucket2 score40 contrasts unreachable(1000,1000) bucket5 score9696 with no primary. Native list head138/tail13c consumes FIFO, one eligible candidate per .1 tick; rejected candidates skip without the eligible budget. Guarded original suffix613d..69a4 runs with constructed canonical registry and actual ResolvePayload/Unit casts, starting AFTER RollDice and stopping BEFORE publication/transport. It proves raw80 changes reachable preference from[2,1,0,8,7,6] to[1,0,2,7,6,8] for roll1, while roll2/3 swap0/1 and6/7. Flags100 or200 insert each corresponding unreachable bucket immediately before its reachable bucket. This suffix analysis is not public producer, RNG-stream or transport parity. +84 periodic and +98 selection remain distinct; Save209 implementation pending.";
  String[][] names={{"6f9d7240","CaptainAI_BeginTargetSelection"},{"6f9d5e00","CaptainAI_OnTargetSelectionTick"},{"6f9cf8a0","CaptainAI_QueueSelectionCandidate"}};
  if(args[0].equals("apply"))for(String[] row:names) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!f.getName().equals(row[1]))throw new Exception("unexpected name "+f.getName());
   String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9d7240","6f9d5e00","6f9cf8a0","6f061320","6f1d73b0","6f684ba0"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
