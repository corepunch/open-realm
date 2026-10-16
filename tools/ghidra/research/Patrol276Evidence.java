import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;

public class Patrol276Evidence extends GhidraScript {
 public static final String[][] NOTES={
  {"6f5fdff0","Move_CreatePatrolTasks","Payoff276: physical engine Patrol legs now use the ordinary scheduled point task, preserving endpoint retry, fine routing, arrival and d0175 FIFO continuation. Two archived300-tick10-unit original captures agree on all3000 public samples; open Move/Patrol pairs agree for58 samples, walled16 and enclosed33. Fresh group Patrol repeats/control agree on405 public markers; all11 leg constructions in each observed run use692120(d016b,range0,NULLassociation) for three distinct units. Singleton legs replace the legacy ai_patrol_walk. Head-on avoidance, facing words and full wall trajectories remain explicitly uncertified."},
  {"6f5fe1a0","Move_ExpandIssuedPatrol","Payoff276: queued single-point Patrol captures origin at activation, before subsequent owner work in the same outer frame. The engine save regression now compares retained continuation words with the synchronous issued-point callback pose, not the later frame-end position. The issued headd0016 can expand tod0017 or retire below100-unit threshold; generic queue code must preserve the owner result rather than restore an assumed issued head."},
  {"6f692120","Unit_CreatePointTask","Payoff276: read-only live hook inside5fdff0 observes11 ordinary point calls per repeat: ECX unit; stack code d016b, X/Y pointer words, range0, associationNULL. GroupPointOrder(Patrol) produces independent per-unit physical legs, matching the explicit NULL passed by5fdff0; the engine regression expects the prior shared Smart owner plus two retired singleton Patrol owners."},
  {"6f5fa7a0","CAbilityMove_OnArrival","Payoff276: Patrol reuses ordinary point completion and executes its d0175 continuation here, including closest-reachable blocked endpoints. Shared scheduled owner commits can continue after the activation boundary inside one game frame. Existing endpoint metadata is preserved across automatic combat and queued rotation; the captured motion regression covers open, walled and enclosed lanes, not complete crowd-avoidance parity."},
  {"6f170fa0","Mover_BindGroup","Payoff276: two additional read-only GroupPointOrder Patrol repeats observe12 non-null physical bindings across four movers: three Patrol and one Move control. Every group has one member; first bindings have four distinct group identities, the entire normalized binding sequence repeats, and all405 public markers equal the unhooked control. This directly supports singleton physical Patrol legs instead of inferring membership solely from a null task association."}
 };
 public void run()throws Exception {
  if(!currentProgram.getExecutableSHA256().equals("d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236"))throw new Exception("wrong original");
  if(getScriptArgs().length!=1)throw new Exception("fresh export required");Path path=Path.of(getScriptArgs()[0]);if(Files.exists(path))throw new Exception("exists");
  StringBuilder out=new StringBuilder();
  for(String[] row:NOTES){
   var address=toAddr(row[0]);var f=getFunctionAt(address);if(f==null)f=createFunction(address,row[1]);if(f==null)throw new Exception(row[0]);
   if(f.getName().startsWith("FUN_"))f.setName(row[1],SourceType.USER_DEFINED);
   var old=f.getComment();if(old!=null)old=old.replaceAll("(?m)^Payoff276[^\\n]*\\n?", "").stripTrailing();
   f.setComment(old==null||old.isEmpty()?row[2]:old+"\n"+row[2]);
   out.append("FUNCTION ").append(row[0]).append(' ').append(f.getName()).append(' ').append(f.getPrototypeString(false,true)).append('\n').append(f.getComment()).append('\n');
   var it=currentProgram.getListing().getInstructions(f.getBody(),true);while(it.hasNext()){var i=it.next();out.append(i.getAddress()).append('|');for(byte b:i.getBytes())out.append(String.format("%02x",b&255));out.append('|').append(i).append('\n');}
   for(var ref:getReferencesTo(address))out.append("XREF ").append(ref).append('\n');
  }
  Files.writeString(path,out);println("Saved "+path);
 }
}
