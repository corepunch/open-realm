// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
public class Modifiers317Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[] args=getScriptArgs();if(args.length!=2)throw new Exception("output and apply|read required");
  var path=Path.of(args[0]);if(Files.exists(path))throw new Exception("new output required");
  String[][] notes={
   {"6f5fb7c0","Move_AdjustSpeedFactor","Payoff317 NUM-03.2: instruction 5fb883 COMISS threshold,abs(distance), followed by 5fb887 CMOVBE, retains unordered factor; decompiled ordered comparison alone is misleading. Public W3A Slow DataA 7fc00001 reaches this owner as ffc00001. Software addition retains that signed word during Bloodlust overlap and cancellation of opposite signed NaNs on removal returns zero. Two completed read-only repeats and unhooked controls agree for NaN and finite Slow/Bloodlust scenes. Engine replay covers 83 physical commits per scene through Stop, captured completion inputs and cold-save continuations. Existing Payoff316 implementation passes; no new engine correction is claimed. Other payloads, casting timing, later scene phases and below-one group republication remain open."},
   {"6f52d490","MoveSpeedBuff_ApplyContribution","Payoff317: observed NaN Slow contribution ffc00001 and finite Bloodlust 3e800000 through actual public spell casts. Replay supplies captured completion boundaries to the existing applying procedures, not synthetic factor setters. Factor, motion and public getter words through Stop are compared; cast scheduling is not certified."},
   {"6f5324c0","MoveSpeedBuff_RemoveContribution","Payoff317: removal in the NaN Slow scene passes inverse 7fc00001 against retained ffc00001; software Math_Add returns zero, not IEEE NaN. The complete physical/public continuation through Stop and cold-save resumes match the existing engine. Ordinary finite overlap removal is also covered."}
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
