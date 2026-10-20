// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Target319Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f169550","PathGroup_RepublishMember","Payoff319: repeated public Follow and Smart against a moving Footman retain target identity and physical arrival range across Slow isolation. Building Follow retains nonnull cached region78 across the same republication. Three scenes, two observations and unhooked control each;775 commits and552 public states; engine cold saves before/after isolation and after target order. No additional republication change beyond318 was needed."},
   {"6f6742f0","CUnit_GetCollisionRadius","Payoff319: runtime-created stock htow returns authored176 world radius; physical15fef0 receives5.5 fine. PathTextures16x16Simple has12 blocked ground texels across, but its192 half-width does not replace authored radius. Engine construction removed footprint-derived collision override."},
   {"6f6945a0","CUnit_RefreshMoverPathingProfile","Payoff319: repeated stock Town Hall creation publishes physical radius5.5 from authored176, independently of texture rectangle. Follow source31 plus target176 yields fine6.46875. The prior engine footprint-derived192 produced6.96875 and failed the first literal movement commit."},
   {"6f5fc640","Move_BeginTargetTaskRequest","Payoff319: runtime JASS CreateUnit(htow) followed by Move target captures a nonnull region and one-shot flag1000; first fine route has18 points ending41.5/28.5. Missing engine runtime footprint publication captured null and produced22 points ending45.5/29.5. Publishing the created building footprint before public return reproduces127 motion commits and184 public states, including Slow republication and three cold-save continuations."},
   {"6f6501a0","Widget_ApplyPathing","Payoff319: building footprint regions and physical circle have independent ownership. Public stock Town Hall target scene retains a nonnull footprint region while authored collision176 remains physical5.5. Engine runtime CreateUnit now publishes its footprint at the admitted pose before return; existing Slow member republication retains that cached identity."}
  };
  StringBuilder out=new StringBuilder();
  for(String[] row:notes){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(args[1].equals("apply")){f.setName(row[1],SourceType.USER_DEFINED);String c=f.getComment();if(c!=null)c=c.replace("conversion at058f11", "conversion at058eef..058f09");f.setComment(c==null||!c.contains(row[2]) ? (c==null?"":c+"\n")+row[2] : c);}
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   out.append("BODY ").append(row[0]).append(' ').append(f.getName()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);
   while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(f.getEntryPoint()))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
