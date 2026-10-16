// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Captain292Evidence extends GhidraScript {
 static final String[][] NOTES={
 {"6f9c02a0","JassNative_StopGathering","Payoff292: AI native registration9be280..28f supplies name b850c0 StopGathering, signature a974ac ()V and callback9c02a0. Current AI identity d77ecc+2c/30 resolves Town, then tail9cc6b0. This is the missing public producer of initial Town selection, not a timer and not an explicit harvest-order cancellation. Two observed repeats and observer-free controls per combat/structure/worker scenario preserve public unit poses and orders."},
 {"6f9cc6b0","TownAI_SelectInitialSpot","Payoff292 corrects291 timer attribution: sole direct incoming tails9c02b7/2c3 are public StopGathering9c02a0. Clear Town2d0 bit0 even with existing spot; existing first spot returns without reselection. Otherwise walk newest-first owned nodes, skip Unit20 bit0, choose first worker248 bit10, else retain last structure68c190(1), else last other unit. Admit through9c01f0(selected,null). Eight original-code selector fixtures control identity/bridge/classification/admission dependencies; live priority and actual Captain default creation are independently observed. Downstream meaning of cleared policy bit0 and automatic economy scheduling are not certified by this chunk."}
 };
 public void run()throws Exception {
  if(!currentProgram.getName().equals("game.dll")||getScriptArgs().length!=1)throw new Exception("game.dll + fresh output");
  var out=Path.of(getScriptArgs()[0]);if(Files.exists(out))throw new Exception("output exists");
  StringBuilder s=new StringBuilder();
  for(var row:NOTES){var a=toAddr(row[0]);var f=getFunctionAt(a);
   if(f==null){disassemble(a);f=createFunction(a,row[1]);}if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   String old=f.getComment()==null ? "" : f.getComment();if(!old.contains(row[2]))f.setComment(old+"\n"+row[2]);
   s.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();s.append(i.getAddress()).append('|');for(byte b:i.getBytes())s.append(String.format("%02x",b&255));s.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(a))s.append("XREF ").append(ref).append('\n');
  }
  for(String key:new String[]{"6f9be280","6f9be285","6f9be28a","6f9be28f"}){var i=getInstructionAt(toAddr(key));s.append(i.getAddress()).append('|');for(byte b:i.getBytes())s.append(String.format("%02x",b&255));s.append('|').append(i).append('\n');}
  Files.writeString(out,s);println("Saved292 producer and selector "+out);
 }
}
