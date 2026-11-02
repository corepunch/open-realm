// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work243Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f89cd10","MoveRequest_ReadyMoverBridge","Payoff243: actual mixed busy/idle Alt+Shift packets mark the three idle movers ready during admission, preserving canonical flags e. The busy mover stays unready. At end of packet the original primary request publishes its ready idle row and is released; the busy mover later reconstructs a fresh flags0 request through queued activation. Do not carry Alt policy into that delayed reconstruction."},
   {"6f16bcf0","MoveRequest_PublishReadyCohorts","Payoff243: two actual Alt+Shift repeats publish FLOAT then special flight then the ready primary Footman at packet completion. Primary first-ready attempt returns -1 until the busy candidate has been admitted; the final packet completion publishes only the ready row. Engine now retains prepared class owners across synchronous idle queue admission; delayed queued starts still rebuild independently. Ordinary Shift repeats retained separately."},
   {"6f6b93a0","NetOrder_PublishUnitOrder","Payoff243: append (flags19) can immediately activate an idle recipient using the retained prepared wrapper. Busy recipient appends without becoming ready. End-of-dispatch publication and later fresh reconstruction are separate boundaries; per-entry saved Alt bits would conflate them."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null)throw new Exception(row[0]);if(!f.getName().equals(row[1])){if(!f.getName().startsWith("FUN_"))throw new Exception("Preserve "+row[0]);f.setName(row[1],ghidra.program.model.symbol.SourceType.USER_DEFINED);}var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f89cd10","6f89ca50","6f16bcf0","6f16d850","6f16bdb0","6f6b93a0"}) {
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
