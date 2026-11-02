// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work239Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f6b9f70","NetOrder_DispatchPointSelectionWithTarget","Payoff239 GROUP-01.1: two genuine singleton selected Footman Alt clicks send packetflags0, not18. Neither calls6b8c10 or89c7c0; each produces one ordinary one-member physical bind with flags0. Preserve engine num_units>1 cohort preparation guard: a UI singleton is not a one-row Alt prepared cohort. Multi-selection flags8/18 and optional Shift1 are distinct. Independent script orders retain independent physical owners; Captain point publications batch12 members under shared parameters, with logical24/25 witnesses already retained by Payoff161. Broader class/lifetime behavior remains GROUP-04.6, not invented by this producer inventory."},
   {"6f6b93a0","NetOrder_PublishUnitOrder","Payoff239: two genuine selected pair then singleton Alt repeats witness Unit240/244 valid shared history being overwritten with ffffffff/ffffffff by the singleton publisher. Unconditional stores6b94e3/6b94ec precede append/admit, including an order with no point association. Engine clears latest request history in the legacy player Move producer before queue/current replacement, independently of the retained current physical owner. Independent JASS producers bypass this net publisher and retain history. No rollback based on admission outcome is justified."},
   {"6f206f00","Jass_DispatchPointOrder","Payoff239 GROUP-01.1 producer inventory: ordinary Move directly creates an order and calls Unit_AdmitOrder; it bypasses NetOrder_PublishUnitOrder6b93a0 and its Unit240/244 history writes. Preserve this producer distinction when clearing selected singleton history."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f6b9f70","6f6b8c10","6f6b93a0","6f206f00","6f5fd270","6f9d1040","6f9d27c0"}) {
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
