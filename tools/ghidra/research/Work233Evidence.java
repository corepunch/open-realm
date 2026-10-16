// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work233Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f16c6d0","PathGroup_SelectRouteSourceMember","Payoff233: complete48 unchanged-original cases verify strict adaptive-enabled/path88.200000 preference over nearer ordinary paths, ties, old-velocity predictions and group80.200 row-zero bypass. Engine reuses saved movement.adaptive_disabled, independent of physical flying class or formation-held flags. Public same-flight rebind/selected group regression fails before fix and preserves the chosen creation-time origin through cold save."},
   {"6f16de50","PathGroup_PrepareRouteFromMembers","Payoff233: group80.200 bypass initializes formation origin from the destination itself, not the first member prediction. Ordinary origins use16c6d0 preference. Existing owned adaptive policy can represent this selection without another saved flag word."},
   {"6f0594a0","UnitMoverBridge_SetAdaptiveEnabled","Payoff233: existing engine movement.adaptive_disabled is the inverse of this authoritative path88.200000 bit.168740 reset and168c00 class updates preserve it. Birth/rebind producer distinction is required for preferred group source selection."}
  };
  for(String[] row:rows) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);
   var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);
  }
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f16c6d0","6f16de50","6f161040","6f15c6c0","6f15c650","6f15cea0","6f0594a0","6f168740","6f168c00"}) {
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
