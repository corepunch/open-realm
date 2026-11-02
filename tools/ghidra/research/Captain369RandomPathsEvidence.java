// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Captain369RandomPathsEvidence extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  if(!currentProgram.getName().equals("game.dll")||args.length!=2||(!args[0].equals("apply")&&!args[0].equals("read")))throw new Exception("game.dll, apply/read and fresh output required");
  if(!"d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236".equals(currentProgram.getExecutableSHA256()))throw new Exception("binary differs");
  Path output=Path.of(args[1]);if(Files.exists(output))throw new Exception("output exists");
  String note="Payoff369 GROUP-03.4.7.3: SetRandomPaths string6fb854f4 registration9be785 binds callback9bfab0. Callback resolves current Town via globald77ecc identity+2c and toggles Town2d0 bit80000, true OR80000 and false ANDfff7ffff. Selection9d5e00 tests precisely this bit before structure destination randomization through9b8690. SetMeleeAI9bf950 only calls TownAI_SetCampaignMode(town,0), whose melee bit is4000; it does not itself define random-path defaults. One Suicide candidate capture selected hbar after stream13 1d3 and then entered9bdad0 stream13 1d2, publishing(-2304,256). Region-table construction, default policy, publication and transport are still pending; no parity claim.";
  String[][] names={{"6f9bfab0","Jass_SetRandomPaths"},{"6f9b8690","TownAI_AdjustStructureAttackDestination"}};
  if(args[0].equals("apply"))for(String[] row:names) {
   var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);
   if(!f.getName().equals(row[1]))throw new Exception("unexpected name "+f.getName());
   String old=f.getComment()==null?"":f.getComment();if(!old.contains(note))f.setComment(old+"\n"+note);
  }
  StringBuilder text=new StringBuilder(note+"\n");
  for(String va:new String[]{"6f9bfab0","6f9b8690","6f9b6620","6f9b7080","6f9b8f50","6f9bce00","6f9bdad0"}) {
   var f=getFunctionAt(toAddr(va));if(f==null)throw new Exception(va);
   text.append("FUNCTION ").append(f.getEntryPoint()).append(' ').append(f.getName()).append('\n').append(f.getComment()).append('\n');
   var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);while(instructions.hasNext()){var i=instructions.next();text.append(i.getAddress()).append('|');for(byte b:i.getBytes())text.append(String.format("%02x",b&255));text.append('|').append(i).append('\n');}
   for(var r:getReferencesTo(f.getEntryPoint()))text.append("XREF ").append(r).append('\n');
  }
  Files.writeString(output,text);println(output.toString());
 }
}
