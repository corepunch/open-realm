// @category WarcraftIII
import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class Work237Evidence extends GhidraScript {
 public void run() throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("game.dll differs");
  String[][] rows={
   {"6f6b8c10","NetOrder_AttachPointCandidates","Payoff237 correction: Unit1fc==10(hex) is FLOAT, not fly2. Primary request includes ordinary foot and fly together. Native selected mixed Footman/Gryphon repeats bind all four to one primary canonical/physical group; Alt instead binds flyers through third request because Unit5c.20000000 is current flight, with forced-ground Unit200<=0 and context18 option required. Historical air_request field name denotes float request. Complete288 native factory/attachment cases retain pending readiness and optional wrapper fallbacks. Engine ordinary non-float selections now share verified cohort admission instead of independent slot fallback; mixed float and Alt current-flight admission remain open."},
   {"6f6ba800","NetOrder_AdmitPointTargetCandidate","Payoff237 correction: context10 primary,14 FLOAT,18 Alt current-flight special. Assembly Unit1fc comparison10(hex) is authored float; raw fly is2. Default mixed foot/fly retains one primary request, not independent ground/air requests. Existing historical air_request field spelling must not be interpreted as flight."}
  };
  for(String[] row:rows){var f=getFunctionAt(toAddr(row[0]));if(f==null||!f.getName().equals(row[1]))throw new Exception("Preserve "+row[0]);var prior=f.getComment();if(prior==null)prior="";if(!prior.contains(row[2]))f.setComment(prior+"\n"+row[2]);}
  StringBuilder out=new StringBuilder();
  for(String a:new String[]{"6f6b8c10","6f6ba800","6f89c7c0","6f89c7f0","6f21e600","6f89c890"}) {
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
