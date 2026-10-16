// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work242Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f687a60","Unit_CountMatchingUserOrders","Payoff242: two new actual selected Shift then replacement repeats retain four candidates with matching/all current-head counts1/1 before Shift and2/2 before replacement. Row1 counts pending Move heads, not only the current head. Engine queue admission now materializes ordinary retained order IDs once, preserving explicit construction/rawcode and private aliases; zero optional IDs resolve the retained name when queried. Two repeats have distinct observation clocks/score words, which remain individually frozen."},
   {"6f6ba800","NetOrder_AdmitPointTargetCandidate","Payoff242: genuine Shift Move packet9 then replacement packet8, four candidates each, verify row1 matching and row2 all heads1->2; Unit1b4 agrees. Complete public marker sequences retained, no new observer-free control. Immediate engine queue-count/save/replacement regression fixes omitted pending identities."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f687a60","6f6ba800","6f6bcc40","6f693490","6f6b93a0"}) {
   var f=getFunctionAt(toAddr(a));if(f==null)throw new Exception(a);
   out.append("FUNCTION ").append(a).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(r).append('\n');
  }
  if(getScriptArgs().length!=1)throw new Exception("new output path required");
  var p=Path.of(getScriptArgs()[0]);if(Files.exists(p))throw new Exception("exists");Files.writeString(p,out);println("Saved "+p);
 }
}
