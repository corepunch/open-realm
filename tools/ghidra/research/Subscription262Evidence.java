// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Subscription262Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f49e130","Attack_HandleTargetAvailable","Payoff262 GROUP-03.2: two complete read-only public owner-transfer repeats and one unhooked control distinguish explicit Attack/AttackOnce from Stop/Attack Move. Same original actor has ability20.4000 clear at ticks2/8 and set at4/6. Only ticks4/6 reach49e351 for that actor, despite all21 handler deliveries/run. Public commands and retained target identities are observed, not synthesized. Engine stores suppression on the actual accepted Attack target owner, retains it across queued/rejected orders and save/load, and clears it at stand/owner release. General49d680 ranking remains outside this bounded guard integration."}
 };
 public void run()throws Exception {
  Path p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("fresh output required");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   String old=f.getComment();if(old==null)old="";if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   out.append("COMMENT ").append(f.getComment()).append('\n').append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  Files.writeString(p,out);println("Saved "+p);
 }
}
